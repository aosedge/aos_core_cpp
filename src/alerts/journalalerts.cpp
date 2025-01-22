/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Format.h>
#include <Poco/RegularExpression.h>

#include <logger/logmodule.hpp>
#include <utils/exception.hpp>

#include "journalalerts.hpp"

namespace aos::sm::alerts {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

Error JournalAlerts::Init(const config::JournalAlertsConfig& config, InstanceInfoProviderItf& instanceInfoProvider,
    StorageItf& storage, aos::alerts::SenderItf& sender)
{
    LOG_DBG() << "Init journal alerts";

    mConfig               = config;
    mInstanceInfoProvider = &instanceInfoProvider;
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

    try {
        SetupJournal();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error JournalAlerts::Start()
{
    LOG_DBG() << "Start journal alerts";

    mStopped       = false;
    mMonitorThread = std::thread(&JournalAlerts::MonitorJournal, this);

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

JournalAlerts::~JournalAlerts()
{
    Stop();
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
    AOS_ERROR_CHECK_AND_THROW("get journal cursor failed", err);

    if (!cursor.IsEmpty()) {
        mJournal->SeekCursor(cursor.CStr());
        mJournal->Next();
    }

    // Set timer.
    Poco::TimerCallback<JournalAlerts> callback(*this, &JournalAlerts::OnTimer);

    mCursorSaveTimer.setStartInterval(cCursorSavePeriod);
    mCursorSaveTimer.setPeriodicInterval(cCursorSavePeriod);
    mCursorSaveTimer.start(callback);
}

// cppcheck-suppress constParameterCallback
void JournalAlerts::OnTimer(Poco::Timer& timer)
{
    (void)timer;

    try {
        std::lock_guard lock {mMutex};

        StoreCurrentCursor();
    } catch (const std::exception& e) {
        LOG_ERR() << "Timer function failed: err=" << AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

void JournalAlerts::StoreCurrentCursor()
{
    auto newCursor = mJournal->GetCursor();
    if (newCursor == mCursor) {
        return;
    }

    auto err = mStorage->SetJournalCursor(newCursor.c_str());
    if (!err.IsNone()) {
        AOS_ERROR_THROW("set journal cursor failed", err);
    }

    mCursor = newCursor;
}

void JournalAlerts::MonitorJournal()
{
    try {
        while (true) {
            std::unique_lock lock {mMutex};

            auto stopped = mCondVar.wait_for(lock, cWaitJournalTimeout, [this] { return mStopped; });
            if (stopped) {
                break;
            }

            ProcessJournal();
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Journal process failed: err=" << AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

void JournalAlerts::ProcessJournal()
{
    while (true) {
        if (!mJournal->Next()) {
            return;
        }
        auto entry = mJournal->GetEntry();

        auto unit = entry.mSystemdUnit;

        if (entry.mSystemdUnit == "init.scope") {
            if (entry.mPriority > mConfig.mServiceAlertPriority) {
                continue;
            }

            unit = entry.mUnit.value_or("");
        }

        // with cgroup v2 logs from container do not contains _SYSTEMD_UNIT due to restrictions
        // that's why id should be extracted from _SYSTEMD_CGROUP
        // format: /system.slice/system-aos@service.slice/AOS_INSTANCE_ID
        if (unit.empty()) {

            // add prefix 'aos-service@' and postfix '.service'
            // to service uuid and get proper seervice object from DB
            unit = entry.mSystemdCGroup;
        }

        cloudprotocol::AlertVariant item;
        if (auto serviceAlert = GetServiceInstanceAlert(entry, unit); serviceAlert.has_value()) {
            item.SetValue<cloudprotocol::ServiceInstanceAlert>(*serviceAlert);
            mSender->SendAlert(item);
        } else if (auto compAlert = GetCoreComponentAlert(entry, unit); compAlert.has_value()) {
            item.SetValue<cloudprotocol::CoreAlert>(*compAlert);
            mSender->SendAlert(item);
        } else if (auto systemAlert = GetSystemAlert(entry); systemAlert.has_value()) {
            item.SetValue<cloudprotocol::SystemAlert>(*systemAlert);
            mSender->SendAlert(item);
        }
    }
}

std::optional<cloudprotocol::ServiceInstanceAlert> JournalAlerts::GetServiceInstanceAlert(
    const utils::JournalEntry& entry, const std::string& unit)
{
    if (mInstanceInfoProvider == nullptr) {
        return std::nullopt;
    }

    if (unit.find(cAosServicePrefix) != std::string::npos) {
        auto instanceID          = ParseInstanceID(unit);
        auto [instanceInfo, err] = mInstanceInfoProvider->GetInstanceInfoByID(instanceID.c_str());
        AOS_ERROR_CHECK_AND_THROW("can't get instance info for unit: " + unit, err);

        auto alert = cloudprotocol::ServiceInstanceAlert(entry.mRealTime);

        alert.mInstanceIdent  = instanceInfo.mInstanceIdent;
        alert.mServiceVersion = instanceInfo.mVersion;
        WriteAlertMsg(entry.mMessage, alert.mMessage);

        return alert;
    }

    return std::nullopt;
}

std::optional<cloudprotocol::CoreAlert> JournalAlerts::GetCoreComponentAlert(
    const utils::JournalEntry& entry, const std::string& unit)
{
    for (const auto& component : cloudprotocol::CoreComponentType::GetStrings()) {
        // cppcheck-suppress useStlAlgorithm
        if (unit.find(component) != std::string::npos) {
            auto alert = cloudprotocol::CoreAlert(entry.mRealTime);

            std::ignore = alert.mCoreComponent.FromString(component);
            WriteAlertMsg(entry.mMessage, alert.mMessage);

            return alert;
        }
    }

    return std::nullopt;
}

std::optional<cloudprotocol::SystemAlert> JournalAlerts::GetSystemAlert(const utils::JournalEntry& entry)
{
    for (const auto& filter : mAlertFilters) {
        auto                           regex = Poco::RegularExpression(filter);
        Poco::RegularExpression::Match match;

        if (regex.match(entry.mMessage, match)) {
            return std::nullopt;
        }
    }

    auto alert = cloudprotocol::SystemAlert(entry.mRealTime);
    WriteAlertMsg(entry.mMessage, alert.mMessage);

    return alert;
}

std::string JournalAlerts::ParseInstanceID(const std::string& unit)
{
    Poco::RegularExpression           regex = Poco::format("%s(.*)\\.service", std::string(cAosServicePrefix));
    Poco::RegularExpression::MatchVec matches;

    if (regex.match(unit, 0, matches) > 1) {
        const auto& group = matches[1];
        std::string id    = unit.substr(group.offset, group.length);

        return id;
    }

    AOS_ERROR_THROW("bad instanceID", ErrorEnum::eFailed);

    return "";
}

void JournalAlerts::WriteAlertMsg(const std::string& src, String& dst)
{
    dst = src.substr(0, dst.MaxSize() - 1).c_str();
}

} // namespace aos::sm::alerts
