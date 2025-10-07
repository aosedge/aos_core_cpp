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

#include <common/logprovider/archivator.hpp>
#include <sm/utils/journal.hpp>

namespace aos::sm::logprovider {

/*
 * Provides service instances ID.
 */
class InstanceIDProviderItf {
public:
    /**
     * Returns service instance IDs.
     *
     * @param filter service instance filter.
     * @return RetWithError<std::vector<std::string>>.
     */
    virtual RetWithError<std::vector<std::string>> GetInstanceIDs(const InstanceFilter& filter) = 0;

    /**
     * Destructor.
     */
    virtual ~InstanceIDProviderItf() = default;
};

/**
 * Provides journal logs.
 */
class LogProvider : public LogProviderItf {
public:
    /**
     * Initializes LogProvider object instance.
     *
     * @param instanceProvider instance provider.
     * @param logReceiver log receiver.
     * @param config log provider config.
     * @return Error.
     */
    Error Init(const aos::logprovider::Config& config, InstanceIDProviderItf& instanceProvider);

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
     * @param observer logs observer.
     * @return Error.
     */
    Error Subscribe(LogObserverItf& observer) override;

    /**
     * Unsubscribes from logs.
     *
     * @param observer logs observer.
     * @return Error.
     */
    Error Unsubscribe(LogObserverItf& observer) override;

private:
    static constexpr auto cAOSServicePrefix = "aos-service@";

    struct GetLogRequest {
        std::vector<std::string> mInstanceIDs;
        StaticString<cLogIDLen>  mLogID;
        Optional<Time>           mFrom, mTill;
        bool                     mCrashLog = false;
    };

    std::shared_ptr<common::logprovider::Archivator> CreateArchivator();
    // to be overridden in unit tests.
    virtual std::shared_ptr<utils::JournalItf> CreateJournal();

    void ScheduleGetLog(const std::vector<std::string>& instanceIDs, const StaticString<cLogIDLen>& logID,
        const Optional<Time>& from, const Optional<Time>& till);

    void ScheduleGetCrashLog(const std::vector<std::string>& instanceIDs, const StaticString<cLogIDLen>& logID,
        const Optional<Time>& from, const Optional<Time>& till);

    void ProcessLogs();

    void GetLog(const std::vector<std::string>& instanceIDs, const StaticString<cLogIDLen>& logID,
        const Optional<Time>& from, const Optional<Time>& till);

    void GetInstanceCrashLog(const std::vector<std::string>& instanceIDs, const StaticString<cLogIDLen>& logID,
        const Optional<Time>& from, const Optional<Time>& till);

    void SendErrorResponse(const String& logID, const std::string& errorMsg);
    void SendEmptyResponse(const String& logID, const std::string& errorMsg);

    void AddServiceCgroupFilter(utils::JournalItf& journal, const std::vector<std::string>& instanceIDs);
    void SeekToTime(utils::JournalItf& journal, const Optional<Time>& from);
    void AddUnitFilter(utils::JournalItf& journal, const std::vector<std::string>& instanceIDs);

    void ProcessJournalLogs(utils::JournalItf& journal, Optional<Time> till, bool needUnitField,
        common::logprovider::Archivator& archivator);
    void ProcessJournalCrashLogs(utils::JournalItf& journal, Time crashTime,
        const std::vector<std::string>& instanceIDs, common::logprovider::Archivator& archivator);

    std::string FormatLogEntry(const utils::JournalEntry& journalEntry, bool addUnit);

    Time        GetCrashTime(utils::JournalItf& journal, const Optional<Time>& from);
    std::string GetUnitNameFromLog(const utils::JournalEntry& entry);
    std::string MakeUnitNameFromInstanceID(const std::string& instanceID);

    InstanceIDProviderItf*   mInstanceProvider = nullptr;
    aos::logprovider::Config mConfig           = {};
    LogObserverItf*          mLogReceiver      = nullptr;

    std::thread               mWorkerThread;
    std::queue<GetLogRequest> mLogRequests;
    std::mutex                mMutex;
    std::condition_variable   mCondVar;
    bool                      mStopped = false;
};

} // namespace aos::sm::logprovider

#endif // LOGPROVIDER_HPP_
