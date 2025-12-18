/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_FSWATCHER_HPP_
#define AOS_COMMON_UTILS_FSWATCHER_HPP_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <sys/inotify.h>
#include <thread>
#include <unordered_map>
#include <vector>

#include <core/common/tools/error.hpp>
#include <core/common/tools/fs.hpp>
#include <core/common/tools/time.hpp>
#include <core/common/tools/timer.hpp>

namespace aos::common::utils {

/**
 * File system watcher.
 */
class FSWatcher : public fs::FSWatcherItf, public NonCopyable {
public:
    /**
     * Initializes file system watcher.
     *
     * @param pollTimeout poll timeout duration.
     * @param events list of fs events to watch.
     * @return Error.
     */
    Error Init(Duration pollTimeout, const std::vector<fs::FSEvent>& events);

    /**
     * Starts file system watcher.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops file system watcher.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Subscribes subscriber on fs events for the specified path.
     *
     * @param path path to watch.
     * @param subscriber subscriber object.
     * @return Error.
     */
    Error Subscribe(const String& path, fs::FSEventSubscriberItf& subscriber) override;

    /**
     * Unsubscribes subscriber.
     *
     * @param path path to unsubscribe from.
     * @param subscriber subscriber to unsubscribe.
     * @return Error.
     */
    Error Unsubscribe(const String& path, fs::FSEventSubscriberItf& subscriber) override;

protected:
    virtual Error StartImpl();
    virtual Error StopImpl();
    virtual Error UnsubscribeImpl(const String& path, fs::FSEventSubscriberItf& subscriber);

private:
    static constexpr size_t cMaxPollEvents = 16;
    static constexpr auto   cWaitTimeout   = std::chrono::seconds(1);

    void                     Run();
    virtual void             NotifySubscribers(const std::vector<fs::FSEventSubscriberItf*>& subscribers,
                    const std::vector<fs::FSEvent>& events, const std::string& path);
    void                     ClearWatchedContexts();
    std::vector<fs::FSEvent> ToFSEvent(uint32_t mask) const;
    size_t                   ToInotifyMask(const std::vector<fs::FSEvent>& events) const;

    struct Context {
        int                                    mFD {-1};
        std::vector<fs::FSEventSubscriberItf*> mSubscribers;
    };

    Duration                                 mPollTimeout {};
    size_t                                   mFlags {};
    int                                      mInotifyFd {-1};
    int                                      mEpollFd {-1};
    int                                      mEventFd {-1};
    std::atomic_bool                         mRunning {};
    bool                                     mInitialized {};
    std::thread                              mThread;
    std::mutex                               mMutex;
    std::mutex                               mSubscribersMutex;
    std::condition_variable                  mCondVar;
    std::unordered_map<std::string, Context> mWatchContexts;
    Timer                                    mTimer;
};

/**
 * File system buffered watcher.
 */
class FSBufferedWatcher : public FSWatcher {
public:
    /**
     * Initializes file system watcher.
     *
     * @param pollTimeout poll timeout duration.
     * @param notifyTimeout notify timeout duration.
     * @param events list of fs events to watch.
     * @return Error.
     */
    Error Init(Duration pollTimeout, Duration notifyTimeout, const std::vector<fs::FSEvent>& events);

protected:
    Error StartImpl() override;
    Error StopImpl() override;
    Error UnsubscribeImpl(const String& path, fs::FSEventSubscriberItf& subscriber) override;

private:
    void NotifySubscribers(const std::vector<fs::FSEventSubscriberItf*>& subscribers,
        const std::vector<fs::FSEvent>& events, const std::string& path) override;
    void Run();

    struct NotifyContext {
        std::vector<fs::FSEventSubscriberItf*> mSubscribers;
        std::vector<fs::FSEvent>               mEvents;
        Time                                   mLastEventTime;
    };

    std::atomic_bool                               mRunning {};
    std::mutex                                     mMutex;
    std::thread                                    mNotifyThread;
    std::condition_variable                        mCondVar;
    Duration                                       mNotifyTimeout;
    std::unordered_map<std::string, NotifyContext> mNotifyContexts;
};

} // namespace aos::common::utils

#endif
