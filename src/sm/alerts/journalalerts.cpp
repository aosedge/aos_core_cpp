/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/RegularExpression.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <sm/launcher/runtimes/container/itf/consts.hpp>

#include "journalalerts.hpp"

namespace aos::sm::alerts {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

// Instance logs don't contain _SYSTEMD_UNIT, instance ID is taken from _SYSTEMD_CGROUP.
// Format: <cCgroupsPath>/<instanceID>[/<subgroup>...]
const Poco::RegularExpression cInstanceCgroupRegex(std::string("^\\Q") + launcher::cCgroupsPath + "\\E/([^/]+)");

} // namespace

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

const std::unordered_map<std::string, CoreComponentType::Enum> JournalAlerts::cCoreComponentServices = {
    {"aos-cm.service", CoreComponentType::Enum::eCM},
    {"aos-sm.service", CoreComponentType::Enum::eSM},
    {"aos-iam.service", CoreComponentType::Enum::eIAM},
};

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error JournalAlerts::Init(const common::config::JournalAlerts& config, InstanceInfoProviderItf* instanceInfoProvider,
    StorageItf& storage, aos::alerts::SenderItf& sender)
{
    LOG_DBG() << "Init journal alerts";

    mConfig               = config;
    mInstanceInfoProvider = instanceInfoProvider;
    mStorage              = &storage;
    mSender               = &sender;

    for (const auto& filter : config.mFilter) {
        if (filter.empty()) {
            LOG_WRN() << "Filter value has an empty string";
            continue;
        }

        // Keep strings instead of precompiled regex because,
        // Poco::RegularExpression suppresses copy/move semantic, consequently they are not supported by stl containers.
        mAlertFilters.emplace_back(filter);
    }

    return ErrorEnum::eNone;
}

Error JournalAlerts::Start()
{
    LOG_DBG() << "Start journal alerts";

    try {
        SetupJournal();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    mStopped       = false;
    mMonitorThread = std::thread(&JournalAlerts::MonitorJournal, this);

    // Start cursor persist thread.
    Poco::TimerCallback<JournalAlerts> callback(*this, &JournalAlerts::OnTimer);

    mCursorSaveTimer.setStartInterval(cCursorSavePeriod);
    mCursorSaveTimer.setPeriodicInterval(cCursorSavePeriod);
    mCursorSaveTimer.start(callback);

    return ErrorEnum::eNone;
}

Error JournalAlerts::Stop()
{
    try {
        {
            std::lock_guard lock {mMutex};

            if (mStopped) {
                return ErrorEnum::eNone;
            }

            LOG_DBG() << "Stop journal alerts";

            mStopped = true;

            mCursorSaveTimer.stop();
            mCondVar.notify_all();
        }

        if (mMonitorThread.joinable()) {
            mMonitorThread.join();
        }

        StoreCurrentCursor();

        mJournal.reset();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

std::shared_ptr<utils::JournalItf> JournalAlerts::CreateJournal()
{
    return std::make_shared<utils::Journal>();
}

void JournalAlerts::SetupJournal()
{
    mJournal = CreateJournal();

    for (int priorityLevel = 0; priorityLevel <= mConfig.mSystemAlertPriority; ++priorityLevel) {
        mJournal->AddMatch("PRIORITY=" + std::to_string(priorityLevel));
    }

    mJournal->AddDisjunction();
    mJournal->AddMatch("_SYSTEMD_UNIT=init.scope");
    mJournal->SeekTail();

    std::ignore = mJournal->Previous();
    StaticString<cJournalCursorLen> cursor;

    auto err = mStorage->GetJournalCursor(cursor);
    AOS_ERROR_CHECK_AND_THROW(err, "get journal cursor failed");

    if (!cursor.IsEmpty()) {
        mJournal->SeekCursor(cursor.CStr());
        mJournal->Next();
    }
}

// cppcheck-suppress constParameterCallback
void JournalAlerts::OnTimer(Poco::Timer& timer)
{
    (void)timer;

    try {
        std::lock_guard lock {mMutex};

        StoreCurrentCursor();
    } catch (const std::exception& e) {
        LOG_WRN() << "Store cursor failed: err=" << AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

void JournalAlerts::StoreCurrentCursor()
{
    if (!mJournal) {
        return;
    }

    auto newCursor = mJournal->GetCursor();
    if (newCursor == mCursor) {
        return;
    }

    auto err = mStorage->SetJournalCursor(newCursor.c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "set journal cursor failed");

    mCursor = newCursor;
}

void JournalAlerts::MonitorJournal()
{
    static constexpr auto cMaxWaitJournalTimeout = 10 * cWaitJournalTimeout;
    auto                  journalWaitTimeout     = cWaitJournalTimeout;

    while (true) {
        std::unique_lock lock {mMutex};

        try {
            const auto waitTimeout = std::chrono::duration_cast<std::chrono::microseconds>(journalWaitTimeout);
            const auto remaining   = mJournal->Wait(waitTimeout);

            auto stopped = mCondVar.wait_for(lock, remaining, [this] { return mStopped; });
            if (stopped) {
                return;
            }

            ProcessJournal();
            journalWaitTimeout = cWaitJournalTimeout;
        } catch (const std::exception& e) {
            LOG_WRN() << "Journal process error: err=" << AOS_ERROR_WRAP(common::utils::ToAosError(e));

            RecoverJournalError();
            journalWaitTimeout = std::min(journalWaitTimeout * 2, cMaxWaitJournalTimeout);
        }
    }
}

void JournalAlerts::ProcessJournal()
{
    while (true) {
        if (!mJournal->Next()) {
            // get cursor to ensure the journal ctx is valid.
            std::ignore = mJournal->GetCursor();

            return;
        }

        auto entry = mJournal->GetEntry();
        auto unit  = entry.mSystemdUnit;

        if (ShouldFilterOutAlert(entry.mMessage)) {
            continue;
        }

        if (entry.mSystemdUnit == "init.scope") {
            if (entry.mPriority > mConfig.mServiceAlertPriority) {
                continue;
            }

            unit = entry.mUnit.value_or("");
        }

        if (unit.empty()) {
            unit = entry.mSystemdCGroup;
        }

        AlertVariant item;

        if (auto instanceAlert = GetInstanceAlert(entry); instanceAlert.has_value()) {
            item.SetValue<InstanceAlert>(*instanceAlert);
            mSender->SendAlert(item);
        } else if (auto compAlert = GetCoreComponentAlert(entry, unit); compAlert.has_value()) {
            item.SetValue<CoreAlert>(*compAlert);
            mSender->SendAlert(item);
        } else if (auto systemAlert = GetSystemAlert(entry); systemAlert.has_value()) {
            item.SetValue<SystemAlert>(*systemAlert);
            mSender->SendAlert(item);
        }
    }
}

void JournalAlerts::RecoverJournalError()
{
    try {
        auto err = mStorage->SetJournalCursor("");
        AOS_ERROR_CHECK_AND_THROW(err, "get journal cursor failed");

        mJournal.reset();
        SetupJournal();
    } catch (const std::exception& e) {
        LOG_ERR() << "Journal monitor recovery failed: err=" << AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

bool JournalAlerts::ShouldFilterOutAlert(const std::string& msg) const
{
    return std::any_of(mAlertFilters.begin(), mAlertFilters.end(), [&msg](const std::string& filter) {
        auto                           regex = Poco::RegularExpression(filter);
        Poco::RegularExpression::Match match;

        return regex.match(msg, match);
    });
}

std::optional<InstanceAlert> JournalAlerts::GetInstanceAlert(const utils::JournalEntry& entry)
{
    if (mInstanceInfoProvider == nullptr) {
        return std::nullopt;
    }

    std::vector<std::string> matches;

    if (cInstanceCgroupRegex.split(entry.mSystemdCGroup, matches) < 2) {
        return std::nullopt;
    }

    const auto& instanceID = matches[1];

    InstanceInfo instanceInfo;

    if (auto err = mInstanceInfoProvider->GetInstanceInfoByID(instanceID.c_str(), instanceInfo); !err.IsNone()) {
        LOG_WRN() << "Can't get instance info" << Log::Field("instanceID", instanceID.c_str()) << Log::Field(err);

        return std::nullopt;
    }

    InstanceAlert alert;

    alert.mTimestamp                   = entry.mRealTime;
    static_cast<InstanceIdent&>(alert) = instanceInfo.mInstanceIdent;
    alert.mVersion                     = instanceInfo.mVersion;

    WriteAlertMsg(entry.mMessage, alert.mMessage);

    return alert;
}

std::optional<CoreAlert> JournalAlerts::GetCoreComponentAlert(const utils::JournalEntry& entry, const std::string& unit)
{
    auto it = std::find_if(cCoreComponentServices.begin(), cCoreComponentServices.end(),
        [&unit](const auto& it) { return unit.find(it.first) != std::string::npos; });
    if (it == cCoreComponentServices.end()) {
        return std::nullopt;
    }

    CoreAlert alert;

    alert.mTimestamp     = entry.mRealTime;
    alert.mCoreComponent = it->second;

    WriteAlertMsg(entry.mMessage, alert.mMessage);

    return alert;
}

std::optional<SystemAlert> JournalAlerts::GetSystemAlert(const utils::JournalEntry& entry)
{
    SystemAlert alert;

    alert.mTimestamp = entry.mRealTime;

    WriteAlertMsg(entry.mMessage, alert.mMessage);

    return alert;
}

void JournalAlerts::WriteAlertMsg(const std::string& src, String& dst)
{
    dst = src.substr(0, dst.MaxSize() - 1).c_str();
}

} // namespace aos::sm::alerts
