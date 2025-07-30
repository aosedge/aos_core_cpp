/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_ALERTS_ALERTS_HPP_
#define AOS_CM_ALERTS_ALERTS_HPP_

#include <condition_variable>
#include <mutex>
#include <thread>

#include <Poco/Timer.h>

#include <aos/cm/communication/communication.hpp>
#include <aos/common/alerts/alerts.hpp>
#include <aos/common/connectionsubsc.hpp>

#include <cm/config/config.hpp>

namespace aos::cm::alerts {

/**
 * Alerts.
 */
class Alerts : public aos::alerts::SenderItf, public ConnectionSubscriberItf {
public:
    /**
     * Initializes alerts.
     *
     * @param config configuration object.
     * @param communication communication object.
     * @return Error.
     */
    Error Init(const config::Alerts& config, communication::CommunicationItf& communication);

    /**
     * Starts alerts module.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops alerts module.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Sends alert data.
     *
     * @param alert alert variant.
     * @return Error.
     */
    Error SendAlert(const cloudprotocol::AlertVariant& alert) override;

    /**
     * Notifies publisher is connected.
     */
    void OnConnect() override;

    /**
     * Notifies publisher is disconnected.
     */
    void OnDisconnect() override;

private:
    void                               SkipAlertsThatOverflowOfflineThreshold();
    bool                               BufferContains(const cloudprotocol::AlertVariant& alert) const;
    bool                               BufferIsFull() const;
    std::vector<cloudprotocol::Alerts> CreateAlertPackages();
    void                               ProcessAlerts(Poco::Timer& timer);

    config::Alerts                   mConfig {};
    communication::CommunicationItf* mCommunication {};

    std::vector<cloudprotocol::AlertVariant> mAlerts;
    size_t                                   mSkippedAlerts {};
    size_t                                   mDuplicatedAlerts {};
    Poco::Timer                              mSendAlertsTimer;

    std::mutex              mMutex;
    std::condition_variable mCondVar;
    std::thread             mThread;
    bool                    mIsRunning   = false;
    bool                    mIsConnected = false;
};

} // namespace aos::cm::alerts

#endif
