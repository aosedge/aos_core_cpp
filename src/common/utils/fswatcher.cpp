/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <vector>

#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>

#include "exception.hpp"
#include "fswatcher.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * FSWatcher
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FSWatcher::Init(Duration pollTimeout, const std::vector<fs::FSEvent>& events)
{
    LOG_DBG() << "Init file system watcher" << Log::Field("pollTimeout", pollTimeout);

    mPollTimeout = pollTimeout;

    mFlags = ToInotifyMask(events);
    if (mFlags == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "no valid fs event specified"));
    }

    mInitialized = true;

    return ErrorEnum::eNone;
}

Error FSWatcher::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start file system watcher";

    return StartImpl();
}

Error FSWatcher::Stop()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Stop file system watcher";

    if (auto err = StopImpl(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "File system watcher stopped";

    return ErrorEnum::eNone;
}

Error FSWatcher::Subscribe(const String& path, fs::FSEventSubscriberItf& subscriber)
{
    std::lock_guard lock {mSubscribersMutex};

    LOG_DBG() << "Start watching" << Log::Field("path", path);

    if (mWatchContexts.find(path.CStr()) == mWatchContexts.end()) {
        int wd = inotify_add_watch(mInotifyFd, path.CStr(), mFlags);
        if (wd < 0) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
        }

        mWatchContexts[path.CStr()].mFD = wd;
    }

    auto& subscribers = mWatchContexts[path.CStr()].mSubscribers;

    if (std::find(subscribers.begin(), subscribers.end(), &subscriber) != subscribers.end()) {
        return ErrorEnum::eAlreadyExist;
    }

    subscribers.push_back(&subscriber);

    return ErrorEnum::eNone;
}

Error FSWatcher::Unsubscribe(const String& path, fs::FSEventSubscriberItf& subscriber)
{
    std::lock_guard lock {mSubscribersMutex};

    LOG_DBG() << "Unsubscribe fs event subscriber" << Log::Field("path", path);

    return UnsubscribeImpl(path, subscriber);
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

Error FSWatcher::StartImpl()
{
    if (mRunning || !mInitialized) {
        return ErrorEnum::eWrongState;
    }

    mInotifyFd = inotify_init1(IN_NONBLOCK);
    if (mInotifyFd < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    Error err;

    auto cleanup = DeferRelease(&err, [this](const Error* err) {
        if (err->IsNone()) {
            return;
        }

        if (mEpollFd >= 0) {
            close(mEpollFd);
            mEpollFd = -1;
        }

        if (mEventFd >= 0) {
            close(mEventFd);
            mEventFd = -1;
        }

        if (mInotifyFd >= 0) {
            close(mInotifyFd);
            mInotifyFd = -1;
        }
    });

    mEventFd = eventfd(0, EFD_NONBLOCK);
    if (mEventFd < 0) {
        err = AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));

        return err;
    }

    mEpollFd = epoll_create1(0);
    if (mEpollFd < 0) {
        err = Error(ErrorEnum::eFailed, strerror(errno));

        return err;
    }

    epoll_event ev {};
    ev.events  = EPOLLIN;
    ev.data.fd = mInotifyFd;

    if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mInotifyFd, &ev) < 0) {
        err = Error(ErrorEnum::eFailed, strerror(errno));

        return err;
    }

    ev.data.fd = mEventFd;
    if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mEventFd, &ev) < 0) {
        err = Error(ErrorEnum::eFailed, strerror(errno));

        return err;
    }

    mRunning = true;

    mThread = std::thread(&FSWatcher::Run, this);

    return ErrorEnum::eNone;
}

Error FSWatcher::StopImpl()
{
    if (!mRunning) {
        return ErrorEnum::eWrongState;
    }

    mRunning = false;

    // Signal eventfd to wake up epoll_wait
    if (mEventFd >= 0) {
        uint64_t value = 1;
        if (write(mEventFd, &value, sizeof(value)) < 0) {
            LOG_WRN() << "Failed to write to eventfd" << Log::Field(Error(errno));
        }
    }

    if (mThread.joinable()) {
        mThread.join();
    }

    ClearWatchedContexts();

    mCondVar.notify_all();

    if (mEventFd >= 0) {
        close(mEventFd);
        mEventFd = -1;
    }

    if (mInotifyFd >= 0) {
        close(mInotifyFd);
        mInotifyFd = -1;
    }

    if (auto err = mTimer.Stop(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error FSWatcher::UnsubscribeImpl(const String& path, fs::FSEventSubscriberItf& subscriber)
{
    auto it = mWatchContexts.find(path.CStr());
    if (it == mWatchContexts.end()) {
        return ErrorEnum::eNotFound;
    }

    auto& pathSubscribers = it->second.mSubscribers;

    pathSubscribers.erase(
        std::remove(pathSubscribers.begin(), pathSubscribers.end(), &subscriber), pathSubscribers.end());

    if (!pathSubscribers.empty()) {
        return ErrorEnum::eNone;
    }

    if (inotify_rm_watch(mInotifyFd, it->second.mFD) < 0) {
        LOG_ERR() << AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    mWatchContexts.erase(it);

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void FSWatcher::Run()
{
    constexpr auto cItemSize = sizeof(struct inotify_event) + cFilePathLen + 1;

    std::vector<char> buffer(cItemSize * cMaxPollEvents);
    epoll_event       events = {};

    while (mRunning) {
        const auto waitResult = epoll_wait(mEpollFd, &events, 1, static_cast<int>(mPollTimeout.Milliseconds()));
        if (waitResult < 0) {
            if (errno != EINTR) {
                LOG_ERR() << "Wait poll event failed" << Log::Field(Error(errno));
            }

            continue;
        } else if (waitResult == 0) {
            continue;
        }

        // Check if woken up by eventfd (stop signal)
        if (events.data.fd == mEventFd) {
            break;
        }

        const auto length = read(mInotifyFd, buffer.data(), buffer.size());
        if (length <= 0) {
            continue;
        }

        int i = 0;

        std::vector<std::pair<int, std::vector<fs::FSEvent>>> notifications;

        while (i < length) {
            struct inotify_event* event = reinterpret_cast<struct inotify_event*>(&buffer[i]);

            i += sizeof(struct inotify_event) + event->len;

            notifications.emplace_back(event->wd, ToFSEvent(event->mask));
        }

        std::lock_guard lock {mSubscribersMutex};

        for (const auto& [wd, wdEvents] : notifications) {
            auto it = std::find_if(
                mWatchContexts.begin(), mWatchContexts.end(), [=](const auto& pair) { return pair.second.mFD == wd; });

            if (it == mWatchContexts.end()) {
                continue;
            }

            NotifySubscribers(it->second.mSubscribers, wdEvents, it->first);
        }
    }
}

void FSWatcher::NotifySubscribers(const std::vector<fs::FSEventSubscriberItf*>& subscribers,
    const std::vector<fs::FSEvent>& events, const std::string& path)
{
    for (const auto subscriber : subscribers) {
        if (!subscriber) {
            continue;
        }

        subscriber->OnFSEvent(path.c_str(), Array<fs::FSEvent>(events.data(), events.size()));
    }
}

void FSWatcher::ClearWatchedContexts()
{
    if (!mRunning) {
        return;
    }

    for (const auto& [path, context] : mWatchContexts) {
        if (context.mFD < 0) {
            continue;
        }

        if (inotify_rm_watch(mInotifyFd, context.mFD) < 0) {
            LOG_WRN() << "Failed to remove inotify watch" << Log::Field("path", path.c_str())
                      << Log::Field(Error(errno));
        }
    }

    mWatchContexts.clear();

    if (mEpollFd >= 0) {
        close(mEpollFd);

        mEpollFd = -1;
    }
}

std::vector<fs::FSEvent> FSWatcher::ToFSEvent(uint32_t mask) const
{
    std::vector<fs::FSEvent> events;

    if (mask & IN_ACCESS) {
        events.push_back(fs::FSEventEnum::eAccess);
    }

    if (mask & IN_MODIFY) {
        events.push_back(fs::FSEventEnum::eModify);
    }

    if (mask & (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)) {
        events.push_back(fs::FSEventEnum::eClose);
    }

    if (mask & (IN_CREATE | IN_MOVED_TO)) {
        events.push_back(fs::FSEventEnum::eCreate);
    }

    if (mask & (IN_DELETE | IN_MOVED_FROM)) {
        events.push_back(fs::FSEventEnum::eDelete);
    }

    return events;
}

size_t FSWatcher::ToInotifyMask(const std::vector<fs::FSEvent>& events) const
{
    size_t mask = 0;

    for (const auto& event : events) {
        switch (event.GetValue()) {
        case fs::FSEventEnum::eAccess:
            mask |= IN_ACCESS;
            break;
        case fs::FSEventEnum::eModify:
            mask |= IN_MODIFY;
            break;
        case fs::FSEventEnum::eClose:
            mask |= IN_CLOSE_WRITE | IN_CLOSE_NOWRITE;
            break;
        case fs::FSEventEnum::eCreate:
            mask |= IN_CREATE | IN_MOVED_TO;
            break;
        case fs::FSEventEnum::eDelete:
            mask |= IN_DELETE | IN_MOVED_FROM;
            break;
        default:
            LOG_WRN() << "Unsupported fs event type" << Log::Field("type", event);
            break;
        }
    }

    return mask;
}

/***********************************************************************************************************************
 * FSBufferedWatcher
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FSBufferedWatcher::Init(Duration pollTimeout, Duration notifyTimeout, const std::vector<fs::FSEvent>& events)
{
    LOG_DBG() << "Init buffered file system watcher" << Log::Field("notifyTimeout", notifyTimeout);

    if (auto err = FSWatcher::Init(pollTimeout, events); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mNotifyTimeout = notifyTimeout;
    if (mNotifyTimeout == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "notify timeout must be greater than zero"));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

Error FSBufferedWatcher::StartImpl()
{
    if (auto err = FSWatcher::StartImpl(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mRunning      = true;
    mNotifyThread = std::thread(&FSBufferedWatcher::Run, this);

    return ErrorEnum::eNone;
}

Error FSBufferedWatcher::StopImpl()
{
    if (auto err = FSWatcher::StopImpl(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mRunning = false;
    mCondVar.notify_all();

    if (mNotifyThread.joinable()) {
        mNotifyThread.join();
    }

    return ErrorEnum::eNone;
}

Error FSBufferedWatcher::UnsubscribeImpl(const String& path, fs::FSEventSubscriberItf& subscriber)
{
    if (auto err = FSWatcher::UnsubscribeImpl(path, subscriber); !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
        return AOS_ERROR_WRAP(err);
    }

    std::lock_guard lock {mMutex};

    auto it = mNotifyContexts.find(path.CStr());
    if (it == mNotifyContexts.end()) {
        return ErrorEnum::eNone;
    }

    auto itSubscribers = std::find(it->second.mSubscribers.begin(), it->second.mSubscribers.end(), &subscriber);
    if (itSubscribers != it->second.mSubscribers.end()) {
        it->second.mSubscribers.erase(itSubscribers);
    }

    if (it->second.mSubscribers.empty()) {
        mNotifyContexts.erase(it);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void FSBufferedWatcher::NotifySubscribers(const std::vector<fs::FSEventSubscriberItf*>& subscribers,
    const std::vector<fs::FSEvent>& events, const std::string& path)
{
    std::lock_guard lock {mMutex};

    if (auto it = mNotifyContexts.find(path); it != mNotifyContexts.end()) {
        auto& context = it->second;

        context.mLastEventTime = Time::Now();
        context.mEvents.insert(context.mEvents.end(), events.begin(), events.end());
        context.mSubscribers = subscribers;
    } else {
        mNotifyContexts.emplace(path, NotifyContext {subscribers, events, Time::Now()});
    }

    mCondVar.notify_all();
}

void FSBufferedWatcher::Run()
{
    while (true) {
        std::unique_lock lock {mMutex};

        mCondVar.wait(lock, [this] { return !mRunning || !mNotifyContexts.empty(); });

        if (!mRunning) {
            return;
        }

        std::vector<std::string> notified;

        auto sleepTimeout = mNotifyTimeout;

        for (auto& [path, context] : mNotifyContexts) {
            if (auto now = Time::Now(); now.Sub(context.mLastEventTime) < mNotifyTimeout) {
                sleepTimeout = std::min(sleepTimeout, mNotifyTimeout - now.Sub(context.mLastEventTime));
                continue;
            }

            for (auto* subscriber : context.mSubscribers) {
                if (!subscriber) {
                    continue;
                }

                subscriber->OnFSEvent(path.c_str(), Array<fs::FSEvent>(context.mEvents.data(), context.mEvents.size()));
            }

            notified.push_back(path);
        }

        for (const auto& path : notified) {
            mNotifyContexts.erase(path);
        }

        if (sleepTimeout > 0) {
            mCondVar.wait_for(lock, std::chrono::nanoseconds(sleepTimeout.Nanoseconds()));
        }
    }
}

} // namespace aos::common::utils
