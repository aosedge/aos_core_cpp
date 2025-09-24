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
#include <sys/inotify.h>
#include <unistd.h>
#include <vector>

#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include <core/common/tools/fs.hpp>

#include <common/logger/logmodule.hpp>

#include "exception.hpp"
#include "fswatcher.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * FSWatcher
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

FSWatcher::~FSWatcher()
{
    LOG_DBG() << "Destroy file system watcher";

    ClearWatchedContexts();

    if (mInotifyFd >= 0) {
        close(mInotifyFd);
    }
}

Error FSWatcher::Init()
{
    LOG_DBG() << "Initialize file system watcher";

    if (mRunning) {
        return ErrorEnum::eWrongState;
    }

    mInotifyFd = inotify_init1(IN_NONBLOCK);
    if (mInotifyFd < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    return ErrorEnum::eNone;
}

Error FSWatcher::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start file system watcher";

    if (mRunning) {
        return ErrorEnum::eWrongState;
    }

    mEpollFd = epoll_create1(0);
    if (mEpollFd < 0) {
        return Error(ErrorEnum::eFailed, strerror(errno));
    }

    epoll_event ev {};
    ev.events  = EPOLLIN;
    ev.data.fd = mInotifyFd;

    if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mInotifyFd, &ev) < 0) {
        auto err = Error(ErrorEnum::eFailed, strerror(errno));

        close(mEpollFd);
        mEpollFd = -1;

        return err;
    }

    mRunning = true;

    mThread = std::thread(&FSWatcher::Run, this);

    return ErrorEnum::eNone;
}

Error FSWatcher::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Stop file system watcher";

        if (!mRunning) {
            return ErrorEnum::eWrongState;
        }

        ClearWatchedContexts();

        mRunning = false;
    }

    mCondVar.notify_all();

    if (mThread.joinable()) {
        mThread.join();
    }

    return ErrorEnum::eNone;
}

Error FSWatcher::Subscribe(const String& path, fs::FSEventSubscriberItf& subscriber)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start watching" << Log::Field("path", path);

    if (mWatchContexts.find(path.CStr()) == mWatchContexts.end()) {
        int wd = inotify_add_watch(mInotifyFd, path.CStr(), mFlags);
        if (wd < 0) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
        }

        mWatchContexts[path.CStr()].mFD   = wd;
        mWatchContexts[path.CStr()].mMask = mFlags;
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
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Unsubscribe fs event subscriber" << Log::Field("path", path);

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

    std::vector<char> buffer(cItemSize * mMaxPollEvents);
    epoll_event       events = {};

    while (true) {
        {
            std::lock_guard lock {mMutex};

            if (!mRunning) {
                return;
            }
        }

        const auto waitResult
            = epoll_wait(mEpollFd, &events, mMaxPollEvents, static_cast<int>(mPollTimeout.Milliseconds()));
        if (waitResult < 0) {
            if (errno != EINTR) {
                LOG_ERR() << "Wait poll event failed" << Log::Field(Error(errno));
            }

            continue;
        } else if (waitResult == 0) {
            continue;
        }

        const auto length = read(mInotifyFd, buffer.data(), buffer.size());
        if (length <= 0) {
            continue;
        }

        int i = 0;

        while (i < length) {
            struct inotify_event* event = reinterpret_cast<struct inotify_event*>(&buffer[i]);

            i += sizeof(struct inotify_event) + event->len;

            std::lock_guard lock {mMutex};

            auto it = std::find_if(mWatchContexts.begin(), mWatchContexts.end(),
                [event](const auto& pair) { return pair.second.mFD == event->wd; });

            if (it == mWatchContexts.end() || !(it->second.mMask & mFlags)) {
                continue;
            }

            auto fsEvents = ToFSEvent(event->mask);

            if (fsEvents.empty()) {
                LOG_WRN() << "Unsupported fs event mask" << Log::Field("mask", event->mask);
                continue;
            }

            for (auto* subscriber : it->second.mSubscribers) {
                if (subscriber == nullptr) {
                    continue;
                }

                subscriber->OnFSEvent(it->first.c_str(), Array<fs::FSEvent>(&fsEvents.front(), fsEvents.size()));
            }
        }
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

} // namespace aos::common::utils
