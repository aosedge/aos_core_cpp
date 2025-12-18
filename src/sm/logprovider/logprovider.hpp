/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LOGPROVIDER_LOGPROVIDER_HPP_
#define AOS_SM_LOGPROVIDER_LOGPROVIDER_HPP_

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <common/logging/archiver.hpp>
#include <core/sm/logging/itf/logprovider.hpp>
#include <sm/utils/journal.hpp>

#include "itf/instanceidprovider.hpp"

namespace aos::sm::logprovider {

/**
 * Provides journal logs.
 */
class LogProvider : public logging::LogProviderItf {
public:
    /**
     * Initializes LogProvider object instance.
     *
     * @param instanceProvider instance provider.
     * @param logReceiver log receiver.
     * @param config log provider config.
     * @return Error.
     */
    Error Init(const aos::logging::Config& config, InstanceIDProviderItf& instanceProvider);

    /**
     * Starts requests processing thread.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops LogProvider.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Destructor.
     */
    ~LogProvider();

    /**
     * Returns service instance log.
     *
     * @param request log request.
     * @return bool.
     */
    Error GetInstanceLog(const RequestLog& request) override;

    /**
     * Returns service instance crash log.
     *
     * @param request log request.
     * @return bool.
     */
    Error GetInstanceCrashLog(const RequestLog& request) override;

    /**
     * Returns system log.
     *
     * @param request log request.
     * @return bool.
     */
    Error GetSystemLog(const RequestLog& request) override;

    /**
     * Subscribes on logs.
     *
     * @param sender log sender.
     * @return Error.
     */
    Error Subscribe(aos::logging::SenderItf& sender);

    /**
     * Unsubscribes from logs.
     *
     * @param sender log sender.
     * @return Error.
     */
    Error Unsubscribe(const aos::logging::SenderItf& sender);

private:
    static constexpr auto cAOSServicePrefix = "aos-service@";

    struct GetLogRequest {
        std::vector<std::string>     mInstanceIDs;
        StaticString<uuid::cUUIDLen> mCorrelationID;
        Optional<Time>               mFrom, mTill;
        bool                         mCrashLog = false;
    };

    std::shared_ptr<common::logging::Archiver> CreateArchiver();
    // to be overridden in unit tests.
    virtual std::shared_ptr<utils::JournalItf> CreateJournal();

    void ScheduleGetLog(const std::vector<std::string>& instanceIDs, const StaticString<uuid::cUUIDLen>& correlationID,
        const Optional<Time>& from, const Optional<Time>& till);

    void ScheduleGetCrashLog(const std::vector<std::string>& instanceIDs,
        const StaticString<uuid::cUUIDLen>& correlationID, const Optional<Time>& from, const Optional<Time>& till);

    void ProcessLogs();

    void GetLog(const std::vector<std::string>& instanceIDs, const StaticString<uuid::cUUIDLen>& correlationID,
        const Optional<Time>& from, const Optional<Time>& till);

    void GetInstanceCrashLog(const std::vector<std::string>& instanceIDs,
        const StaticString<uuid::cUUIDLen>& correlationID, const Optional<Time>& from, const Optional<Time>& till);

    void SendErrorResponse(const String& correlationID, const std::string& errorMsg);
    void SendEmptyResponse(const String& correlationID, const std::string& errorMsg);

    void AddServiceCgroupFilter(utils::JournalItf& journal, const std::vector<std::string>& instanceIDs);
    void SeekToTime(utils::JournalItf& journal, const Optional<Time>& from);
    void AddUnitFilter(utils::JournalItf& journal, const std::vector<std::string>& instanceIDs);

    void ProcessJournalLogs(
        utils::JournalItf& journal, Optional<Time> till, bool needUnitField, common::logging::Archiver& archiver);
    void ProcessJournalCrashLogs(utils::JournalItf& journal, Time crashTime,
        const std::vector<std::string>& instanceIDs, common::logging::Archiver& archiver);

    std::string FormatLogEntry(const utils::JournalEntry& journalEntry, bool addUnit);

    Time        GetCrashTime(utils::JournalItf& journal, const Optional<Time>& from);
    std::string GetUnitNameFromLog(const utils::JournalEntry& entry);
    std::string MakeUnitNameFromInstanceID(const std::string& instanceID);

    InstanceIDProviderItf*   mInstanceProvider = nullptr;
    aos::logging::Config     mConfig           = {};
    aos::logging::SenderItf* mLogSender        = nullptr;

    std::thread               mWorkerThread;
    std::queue<GetLogRequest> mLogRequests;
    std::mutex                mMutex;
    std::condition_variable   mCondVar;
    bool                      mStopped = false;
};

} // namespace aos::sm::logprovider

#endif // LOGPROVIDER_HPP_
