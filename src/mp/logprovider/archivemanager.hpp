/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ARCHIVERMANAGER_HPP_
#define ARCHIVERMANAGER_HPP_

#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Poco/Timer.h>

#include <aos/common/tools/error.hpp>
#include <logprovider/archivator.hpp>
#include <servicemanager/v4/servicemanager.grpc.pb.h>

namespace aos::mp::logprovider {

/**
 * Archive manager class.
 */
class ArchiveManager {
public:
    /**
     * Initializes archive manager.
     *
     * @param logReceiver log receiver.
     * @param config configuration.
     * @return Error.
     */
    Error Init(sm::logprovider::LogObserverItf& logReceiver, const common::logprovider::Config& config);

    /**
     * Starts archive manager.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops archive manager.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Handles log data.
     *
     * @param log log data.
     * @return Error.
     */
    Error HandleLog(std::shared_ptr<servicemanager::v4::LogData> log);

private:
    static constexpr auto cLogCleanupPeriod  = Time::cMinutes * 5;
    static constexpr auto cLogPendingTimeout = Time::cSeconds * 10;

    struct ArchiveContext {
        ArchiveContext(const std::string& logID, sm::logprovider::LogObserverItf& logReceiver,
            const common::logprovider::Config& config)
            : mLogID(logID)
            , mUpdated(Time::Now())
            , mArchivator(logReceiver, config)
        {
        }

        std::string                     mLogID;
        Time                            mUpdated;
        common::logprovider::Archivator mArchivator;
    };

    void                     Run();
    void                     CleanupOutdatedArchives(Poco::Timer& timer);
    Error                    Archive(std::shared_ptr<servicemanager::v4::LogData> log);
    Error                    SendFinalChunk(std::shared_ptr<ArchiveContext> archiveContext);
    cloudprotocol::LogStatus GetLogStatus(const std::shared_ptr<servicemanager::v4::LogData>& log) const;

    Poco::Timer                                              mTimer;
    bool                                                     mStopped = true;
    std::thread                                              mThread;
    std::condition_variable                                  mCondVar;
    std::mutex                                               mMutex;
    common::logprovider::Config                              mConfig      = {};
    sm::logprovider::LogObserverItf*                         mLogReceiver = {};
    std::vector<std::shared_ptr<ArchiveContext>>             mArchiveContexts;
    std::queue<std::shared_ptr<servicemanager::v4::LogData>> mLogQueue;
};

} // namespace aos::mp::logprovider

#endif
