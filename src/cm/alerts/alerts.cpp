/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/logger/logmodule.hpp>

#include "alerts.hpp"

namespace aos::cm::alerts {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class GetTimestamp : public StaticVisitor<Time> {
public:
    Res Visit(const cloudprotocol::AlertItem& alert) const { return alert.mTimestamp; }
};

class SetTimestamp : public StaticVisitor<void> {
public:
    explicit SetTimestamp(const Time& time)
        : mTime(time)
    {
    }

    template <typename T>
    Res Visit(T& val) const
    {
        val.mTimestamp = mTime;
    }

private:
    Time mTime;
};

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Alerts::Init(const config::Alerts& config, communication::CommunicationItf& communication)
{
    LOG_DBG() << "Initialize alerts";

    mConfig        = config;
    mCommunication = &communication;

    return ErrorEnum::eNone;
}

Error Alerts::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start alerts module";

    if (mIsRunning) {
        return ErrorEnum::eWrongState;
    }

    Poco::TimerCallback<Alerts> callback(*this, &Alerts::ProcessAlerts);

    mSendAlertsTimer.setStartInterval(mConfig.mSendPeriod.Milliseconds());
    mSendAlertsTimer.setPeriodicInterval(mConfig.mSendPeriod.Milliseconds());
    mSendAlertsTimer.start(callback);

    mIsRunning = true;

    return ErrorEnum::eNone;
}

Error Alerts::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Stop alerts module";

        if (!mIsRunning) {
            return ErrorEnum::eWrongState;
        }

        mSendAlertsTimer.stop();
        mIsRunning = false;
    }

    return ErrorEnum::eNone;
}

Error Alerts::SendAlert(const cloudprotocol::AlertVariant& alert)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send alert" << Log::Field("alert", alert);

    if (BufferContains(alert)) {
        mDuplicatedAlerts++;

        return ErrorEnum::eNone;
    }

    if (BufferIsFull()) {
        mSkippedAlerts++;

        return Error(ErrorEnum::eNoMemory, "alert buffer is full");
    }

    mAlerts.push_back(alert);

    return ErrorEnum::eNone;
}

void Alerts::OnConnect()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Publisher connected";

    mIsConnected = true;
}

void Alerts::OnDisconnect()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Publisher disconnected";

    mIsConnected = false;

    SkipAlertsThatOverflowOfflineThreshold();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Alerts::SkipAlertsThatOverflowOfflineThreshold()
{

    if (mAlerts.size() > static_cast<size_t>(mConfig.mMaxOfflineMessages) * cloudprotocol::cAlertItemsCount) {
        mSkippedAlerts += mAlerts.size() - mConfig.mMaxOfflineMessages;

        mAlerts.resize(mConfig.mMaxOfflineMessages);
    }
}

bool Alerts::BufferContains(const cloudprotocol::AlertVariant& alert) const
{
    auto alertCopy = std::make_unique<cloudprotocol::AlertVariant>(alert);

    return std::find_if(mAlerts.begin(), mAlerts.end(), [&alertCopy](const auto& alert) {
        alertCopy->ApplyVisitor(SetTimestamp(alert.ApplyVisitor(GetTimestamp())));

        return *alertCopy == alert;
    }) != mAlerts.end();
}

bool Alerts::BufferIsFull() const
{
    if (mIsConnected) {
        return false;
    }

    return mAlerts.size() >= static_cast<size_t>(mConfig.mMaxOfflineMessages) * cloudprotocol::cAlertItemsCount;
}

std::vector<cloudprotocol::Alerts> Alerts::CreateAlertPackages()
{
    std::vector<cloudprotocol::Alerts> alertItems;
    alertItems.reserve(mConfig.mMaxOfflineMessages);

    for (size_t i = 0; i < mAlerts.size(); ++i) {
        if (alertItems.empty() || alertItems.back().mItems.IsFull()) {
            alertItems.emplace_back();
        }

        alertItems.back().mItems.EmplaceBack(mAlerts[i]);
    }

    return alertItems;
}

void Alerts::ProcessAlerts(Poco::Timer&)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Process alerts";

    if (!mIsRunning || !mIsConnected || mAlerts.empty()) {
        return;
    }

    if (mSkippedAlerts > 0) {
        LOG_WRN() << "Alerts skipped due to channel is full" << Log::Field("count", mSkippedAlerts);
    }

    if (mDuplicatedAlerts > 0) {
        LOG_WRN() << "Alerts skipped due to duplication" << Log::Field("count", mDuplicatedAlerts);
    }

    auto msg = std::make_unique<cloudprotocol::MessageVariant>();

    for (const auto& alertPackage : CreateAlertPackages()) {
        msg->SetValue(alertPackage);

        if (auto err = mCommunication->SendMessage(*msg); !err.IsNone()) {
            LOG_ERR() << "Can't send alert" << Log::Field(err);
        }
    }

    mAlerts.clear();

    mSkippedAlerts    = 0;
    mDuplicatedAlerts = 0;
}

} // namespace aos::cm::alerts
