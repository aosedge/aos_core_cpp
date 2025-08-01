/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_MONITORING_MONITORING_HPP_
#define AOS_CM_MONITORING_MONITORING_HPP_

#include <condition_variable>
#include <mutex>
#include <thread>

#include <Poco/Timer.h>

#include <core/cm/communication/communication.hpp>
#include <core/common/cloudprotocol/monitoring.hpp>
#include <core/common/connectionprovider/connectionprovider.hpp>
#include <core/common/monitoring/monitoring.hpp>

#include <cm/config/config.hpp>

namespace aos::cm::monitoring {

/**
 * Monitoring.
 */
class Monitoring : public aos::monitoring::SenderItf, public ConnectionSubscriberItf {
public:
    /**
     * Initializes alerts.
     *
     * @param config configuration object.
     * @param communication communication object.
     * @return Error.
     */
    Error Init(const config::Monitoring& config, communication::CommunicationItf& communication);

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
     * Sends monitoring data.
     *
     * @param monitoringData monitoring data.
     * @return Error.
     */
    Error SendMonitoringData(const aos::monitoring::NodeMonitoringData& monitoringData) override;

    /**
     * Notifies publisher is connected.
     */
    void OnConnect() override;

    /**
     * Notifies publisher is disconnected.
     */
    void OnDisconnect() override;

private:
    bool  CanAddNodesToLastPackage(const String& nodeID) const;
    bool  CanAddServiceInstancesToLastPackage(const aos::monitoring::NodeMonitoringData& monitoringData) const;
    void  AdjustMonitoringCache(const aos::monitoring::NodeMonitoringData& monitoringData);
    Error FillInstanceMonitoring(
        const String& nodeID, const Time& timestamp, const aos::monitoring::InstanceMonitoringData& instanceMonitoring);
    Error FillNodeMonitoring(
        const String& nodeID, const Time& timestamp, const aos::monitoring::NodeMonitoringData& nodeMonitoring);
    Error CacheMonitoringData(const aos::monitoring::NodeMonitoringData& monitoringData);
    void  ProcessMonitoring(Poco::Timer& timer);

    config::Monitoring               mConfig {};
    communication::CommunicationItf* mCommunication {};

    std::vector<cloudprotocol::Monitoring> mMonitoring;
    Poco::Timer                            mSendMonitoringTimer;

    std::mutex              mMutex;
    std::condition_variable mCondVar;
    std::thread             mThread;
    bool                    mIsRunning   = false;
    bool                    mIsConnected = false;
};

} // namespace aos::cm::monitoring

#endif
