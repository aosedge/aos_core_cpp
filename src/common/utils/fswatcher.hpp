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

#include <core/common/tools/error.hpp>
#include <core/common/tools/time.hpp>

namespace aos::common::utils {

class FSEventSubscriber {
public:
    /**
     * Called when file system event occurs for a specified path.
     *
     * @param path path to the file or directory that triggered the event.
     * @param mask bitmask of events that occurred, e.g., IN_MODIFY, IN_CREATE, IN_DELETE.
     */
    virtual void OnFSEvent(const std::string& path, uint32_t mask) = 0;

    /**
     * Destructor.
     */
    virtual ~FSEventSubscriber() = default;
};

/**
 * File system watcher.
 */
class FSWatcher : public NonCopyable {
public:
    /**
     * Constructs file system watcher.
     *
     * @param pollTimeout poll timeout duration.
     * @param maxPollEvents maximum number of poll events to process in one iteration.
     */
    explicit FSWatcher(Duration pollTimeout = Time::cSeconds * 5, size_t maxPollEvents = 1, size_t flags = IN_MODIFY)
        : mPollTimeout(pollTimeout)
        , mMaxPollEvents(maxPollEvents)
        , mFlags(flags)
    {
    }

    /**
     * Initializes file system watcher.
     *
     * @return Error.
     */
    Error Init();

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
    Error Subscribe(const std::string& path, FSEventSubscriber& subscriber);

    /**
     * Unsubscribes subscriber.
     *
     * @param path path to unsubscribe from.
     * @param subscriber subscriber to unsubscribe.
     * @return Error.
     */
    Error Unsubscribe(const std::string& path, FSEventSubscriber& subscriber);

    /**
     * Destructor.
     */
    ~FSWatcher();

private:
    void Run();
    void ClearWatchedContexts();

    struct Context {
        int                             mFD = -1;
        std::vector<FSEventSubscriber*> mSubscribers;
        uint32_t                        mMask = 0;
    };

    Duration                                 mPollTimeout;
    size_t                                   mMaxPollEvents = 1;
    size_t                                   mFlags         = IN_MODIFY;
    int                                      mInotifyFd     = -1;
    int                                      mEpollFd       = -1;
    bool                                     mRunning       = false;
    std::thread                              mThread;
    std::mutex                               mMutex;
    std::condition_variable                  mCondVar;
    std::unordered_map<std::string, Context> mWatchContexts;
};

} // namespace aos::common::utils

#endif
