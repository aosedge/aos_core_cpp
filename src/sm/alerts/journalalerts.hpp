/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JOURNALALERTS_HPP_
#define JOURNALALERTS_HPP_

#include <Poco/Timer.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <aos/common/cloudprotocol/cloudprotocol.hpp>

#include <sm/config/config.hpp>
#include <sm/utils/journal.hpp>

#include "alerts.hpp"

namespace aos::sm::alerts {

/**
 * Journal alerts.
 */
class JournalAlerts {
public:
    /**
     * Initializes object instance.
     *
     * @param config alerts config.
     * @param instanceInfoProvider instance info provider.
     * @param storage alerts storage.
     * @param sender alerts sender.
     * @return Error.
     */
    Error Init(const config::JournalAlertsConfig& config, InstanceInfoProviderItf& instanceInfoProvider,
        StorageItf& storage, aos::alerts::SenderItf& sender);

    /**
     * Starts journal monitoring thread.
     */
    Error Start();

    /**
     * Stops object instance.
     */
    Error Stop();

private:
    static constexpr auto cWaitJournalTimeout = std::chrono::seconds(1);
    static constexpr auto cCursorSavePeriod   = 10 * 1000; // ms.
    static constexpr auto cAosServicePrefix   = "aos-service@";
    static constexpr auto cJournalCursorLen   = 128;

    // to be overridden in unit tests.
    virtual std::shared_ptr<utils::JournalItf> CreateJournal();

    void SetupJournal();
    void OnTimer(Poco::Timer& timer);
    void StoreCurrentCursor();
    void MonitorJournal();
    void ProcessJournal();
    void RecoverJournalError();
    bool ShouldFilterOutAlert(const std::string& msg) const;

    std::optional<cloudprotocol::ServiceInstanceAlert> GetServiceInstanceAlert(
        const utils::JournalEntry& entry, const std::string& unit);
    std::optional<cloudprotocol::CoreAlert> GetCoreComponentAlert(
        const utils::JournalEntry& entry, const std::string& unit);
    std::optional<cloudprotocol::SystemAlert> GetSystemAlert(const utils::JournalEntry& entry);
    std::string                               ParseInstanceID(const std::string& unit);
    void                                      WriteAlertMsg(const std::string& src, String& dst);

    config::JournalAlertsConfig mConfig               = {};
    InstanceInfoProviderItf*    mInstanceInfoProvider = nullptr;
    StorageItf*                 mStorage              = nullptr;
    aos::alerts::SenderItf*     mSender               = nullptr;

    std::vector<std::string> mAlertFilters;
    Poco::Timer              mCursorSaveTimer;
    std::thread              mMonitorThread;
    std::mutex               mMutex;
    std::condition_variable  mCondVar;
    bool                     mStopped = true;
    std::string              mCursor;

    std::shared_ptr<utils::JournalItf> mJournal;
};

} // namespace aos::sm::alerts

#endif
