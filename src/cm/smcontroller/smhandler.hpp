/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_SMHANDLER_HPP_
#define AOS_CM_SMCONTROLLER_SMHANDLER_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <servicemanager/v5/servicemanager.grpc.pb.h>

#include <common/utils/syncmessagesender.hpp>
#include <core/cm/alerts/itf/receiver.hpp>
#include <core/cm/launcher/itf/instancestatusreceiver.hpp>
#include <core/cm/launcher/itf/sender.hpp>
#include <core/cm/monitoring/itf/receiver.hpp>
#include <core/cm/nodeinfoprovider/itf/sminforeceiver.hpp>
#include <core/cm/smcontroller/itf/sender.hpp>
#include <core/common/types/envvars.hpp>
#include <core/common/types/instance.hpp>
#include <core/common/types/log.hpp>
#include <core/common/types/network.hpp>
#include <core/common/types/unitconfig.hpp>

namespace aos::cm::smcontroller {

/**
 * Message structure for synchronous communication.
 */
struct Message {
    servicemanager::v5::SMOutgoingMessages* mOutputMessage;
    std::condition_variable                 mCondVar;
    std::mutex                              mMutex;
    bool                                    mResponseReceived {};
};

/**
 * Node connection status listener interface.
 */
class NodeConnectionStatusListenerItf {
public:
    virtual ~NodeConnectionStatusListenerItf() = default;

    /**
     * Called when SM client of the node connects.
     *
     * @param nodeID node identifier.
     */
    virtual void OnNodeConnected(const String& nodeID) = 0;

    /**
     * Called when SM client of the node disconnects.
     *
     * @param nodeID node identifier.
     */
    virtual void OnNodeDisconnected(const String& nodeID) = 0;
};

/**
 * Handles communication with a specific Service Manager on a node.
 */
class SMHandler {
public:
    /**
     * Constructor.
     *
     * @param context grpc server context.
     * @param stream grpc stream.
     * @param alertsReceiver alerts receiver.
     * @param logSender log sender.
     * @param envVarsStatusSender environment variables status sender.
     * @param monitoringReceiver monitoring receiver.
     * @param instanceStatusReceiver instance status receiver.
     * @param smInfoReceiver SM info receiver.
     */
    SMHandler(grpc::ServerContext* context,
        grpc::ServerReaderWriter<servicemanager::v5::SMIncomingMessages, servicemanager::v5::SMOutgoingMessages>*
                             stream,
        alerts::ReceiverItf& alertsReceiver, SenderItf& logSender, launcher::SenderItf& envVarsStatusSender,
        monitoring::ReceiverItf& monitoringReceiver, launcher::InstanceStatusReceiverItf& instanceStatusReceiver,
        nodeinfoprovider::SMInfoReceiverItf& smInfoReceiver, NodeConnectionStatusListenerItf& connStatusListener);

    /**
     * Starts handling the node communication.
     */
    void Start();

    /**
     * Blocks until the node communication is stopped.
     */
    void Wait();

    /**
     * Stops handling the node communication.
     */
    void Stop();

    /**
     * Get the Node ID.
     *
     * @return String.
     */
    String GetNodeID() const;

    /**
     * Gets node config status.
     *
     * @param status node config status.
     * @return Error code.
     */
    Error GetNodeConfigStatus(NodeConfigStatus& status);

    /**
     * Checks node config.
     *
     * @param config node config.
     * @return Error code.
     */
    Error CheckNodeConfig(const NodeConfig& config);

    /**
     * Updates node config.
     *
     * @param config node config.
     * @return Error code.
     */
    Error UpdateNodeConfig(const NodeConfig& config);

    /**
     * Requests log.
     *
     * @param log log request.
     * @return Error code.
     */
    Error RequestLog(const aos::RequestLog& log);

    /**
     * Updates network parameters.
     *
     * @param networkParameters network parameters.
     * @return Error code.
     */
    Error UpdateNetworks(const Array<UpdateNetworkParameters>& networkParameters);

    /**
     * Updates instances.
     *
     * @param stopInstances instances to stop.
     * @param startInstances instances to start.
     * @return Error code.
     */
    Error UpdateInstances(
        const Array<aos::InstanceInfo>& stopInstances, const Array<aos::InstanceInfo>& startInstances);

    /**
     * Gets average monitoring data.
     *
     * @param monitoring average monitoring data.
     * @return Error code.
     */
    Error GetAverageMonitoring(aos::monitoring::NodeMonitoringData& monitoring);

    /**
     * Handles cloud connected event.
     */
    void OnConnect();

    /**
     * Handles cloud disconnected event.
     */
    void OnDisconnect();

private:
    static constexpr auto cResponseTime = std::chrono::seconds(5);

    Error SendMessage(const servicemanager::v5::SMIncomingMessages& message);

    // Message processing methods
    Error ProcessMessages();

    Error ProcessSMInfo(const servicemanager::v5::SMInfo& smInfo);
    Error ProcessUpdateInstancesStatus(const servicemanager::v5::UpdateInstancesStatus& status);
    Error ProcessNodeInstancesStatus(const servicemanager::v5::NodeInstancesStatus& status);
    Error ProcessLogData(const servicemanager::v5::LogData& logData);
    Error ProcessInstantMonitoring(const servicemanager::v5::InstantMonitoring& monitoring);
    Error ProcessAlert(const servicemanager::v5::Alert& alert);

    grpc::ServerContext* mContext {};
    grpc::ServerReaderWriter<servicemanager::v5::SMIncomingMessages, servicemanager::v5::SMOutgoingMessages>*
        mStream {};
    common::utils::SyncMessageSender<servicemanager::v5::SMIncomingMessages, servicemanager::v5::SMOutgoingMessages>
        mSyncMessageSender;

    alerts::ReceiverItf*                 mAlertsReceiver {};
    SenderItf*                           mLogSender {};
    launcher::SenderItf*                 mEnvVarsStatusSender {};
    monitoring::ReceiverItf*             mMonitoringReceiver {};
    launcher::InstanceStatusReceiverItf* mInstanceStatusReceiver {};
    nodeinfoprovider::SMInfoReceiverItf* mSMInfoReceiver {};
    NodeConnectionStatusListenerItf*     mConnStatusListener {};

    std::mutex           mMutex;
    bool                 mCredentialListUpdated {};
    grpc::ServerContext* mCtx {};

    std::thread       mProcessThread;
    std::atomic<bool> mStopProcessing {};

    StaticString<cIDLen> mNodeID;
};

} // namespace aos::cm::smcontroller

#endif
