/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_SMCONTROLLER_HPP_
#define AOS_CM_SMCONTROLLER_SMCONTROLLER_HPP_

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <grpcpp/server.h>
#include <servicemanager/v5/network.grpc.pb.h>
#include <servicemanager/v5/servicemanager.grpc.pb.h>

#include <core/cm/imagemanager/itf/iteminfoprovider.hpp>
#include <core/cm/launcher/itf/envvarhandler.hpp>
#include <core/cm/launcher/itf/instancerunner.hpp>
#include <core/cm/launcher/itf/monitoringprovider.hpp>
#include <core/cm/nodeinfoprovider/itf/sminforeceiver.hpp>
#include <core/cm/smcontroller/itf/logprovider.hpp>
#include <core/cm/smcontroller/itf/smcontroller.hpp>
#include <core/cm/unitconfig/itf/nodeconfighandler.hpp>
#include <core/common/cloudconnection/itf/cloudconnection.hpp>
#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/networkmanager/itf/networkprovider.hpp>
#include <core/common/networkmanager/itf/pendingupdatehandler.hpp>
#include <core/common/tools/timer.hpp>

#include "config.hpp"
#include "smhandler.hpp"

namespace aos::cm::smcontroller {

/**
 * Service Manager Controller.
 */
class SMController : public SMControllerItf,
                     public aos::networkmanager::PendingUpdateHandlerItf,
                     private cloudconnection::ConnectionListenerItf,
                     private NodeConnectionStatusListenerItf,
                     private aos::iamclient::CertListenerItf,
                     private servicemanager::v5::SMService::Service,
                     private servicemanager::v5::NetworkService::Service {
public:
    /**
     * Initializes the SM controller.
     *
     * @param config configuration.
     * @param cloudConnection cloud connection.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param itemInfoProvider item info provider.
     * @param alertsReceiver alerts receiver.
     * @param logSender log sender.
     * @param envVarsStatusSender env vars status sender.
     * @param monitoringReceiver monitoring receiver.
     * @param instanceStatusReceiver instance status receiver.
     * @param smInfoReceiver SM info receiver.
     * @param insecureConn true if insecure connection is used.
     * @return Error.
     */
    Error Init(const Config& config, cloudconnection::CloudConnectionItf& cloudConnection,
        aos::iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
        crypto::x509::ProviderItf& cryptoProvider, imagemanager::ItemInfoProviderItf& itemInfoProvider,
        alerts::ReceiverItf& alertsReceiver, SenderItf& logSender, launcher::SenderItf& envVarsStatusSender,
        monitoring::ReceiverItf& monitoringReceiver, launcher::InstanceStatusReceiverItf& instanceStatusReceiver,
        nodeinfoprovider::SMInfoReceiverItf& smInfoReceiver, aos::networkmanager::NetworkProviderItf& networkProvider,
        bool insecureConn = false);

    /**
     * Starts the SM controller.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops the SM controller.
     *
     * @return Error.
     */
    Error Stop();

    //
    // NodeConfigHandlerItf interface methods
    //

    /**
     * Checks node config.
     *
     * @param nodeID Node ID.
     * @param config Node config.
     * @return Error.
     */
    Error CheckNodeConfig(const String& nodeID, const NodeConfig& config) override;

    /**
     * Updates node config.
     *
     * @param nodeID Node ID.
     * @param config Node config.
     * @return Error.
     */
    Error UpdateNodeConfig(const String& nodeID, const NodeConfig& config) override;

    /**
     * Returns node config status.
     *
     * @param nodeID Node ID.
     * @param status Node config status.
     * @return Error.
     */
    Error GetNodeConfigStatus(const String& nodeID, NodeConfigStatus& status) override;

    //
    // LogProviderItf interface methods
    //

    /**
     * Requests log.
     *
     * @param log log request.
     * @return Error.
     */
    Error RequestLog(const aos::RequestLog& log) override;

    //
    // InstanceRunnerItf interface methods
    //

    /**
     * Updates instances on specified node.
     *
     * @param nodeID Node ID.
     * @param stopInstances Instance list to stop.
     * @param startInstances Instance list to start.
     * @return Error.
     */
    Error UpdateInstances(const String& nodeID, const Array<aos::InstanceInfo>& stopInstances,
        const Array<aos::InstanceInfo>& startInstances) override;

    //
    // MonitoringProviderItf interface methods
    //

    /**
     * Returns monitoring data for a node.
     *
     * @param nodeID Node ID.
     * @param monitoring Monitoring data.
     * @return Error.
     */
    Error GetAverageMonitoring(const String& nodeID, aos::monitoring::NodeMonitoringData& monitoring) override;

    //
    // PendingUpdateHandlerItf interface methods
    //

    /**
     * Called when pending firewall rules are resolved for an instance.
     *
     * @param nodeID node ID where the instance resides.
     * @param update pending firewall update.
     */
    void OnPendingFirewallUpdate(
        const String& nodeID, const aos::networkmanager::PendingFirewallUpdate& update) override;

private:
    static constexpr Duration cReconnectRetryTimeout = Time::cSeconds * 10;

    //
    // ConnectionListenerItf interface methods
    //

    void OnConnect() override;
    void OnDisconnect() override;

    //
    // SMService::Service GRPC service methods
    //

    grpc::Status RegisterSM(grpc::ServerContext* context,
        grpc::ServerReaderWriter<servicemanager::v5::SMIncomingMessages, servicemanager::v5::SMOutgoingMessages>*
            stream) override;

    grpc::Status GetBlobsInfos(grpc::ServerContext* context, const servicemanager::v5::BlobsInfosRequest* request,
        servicemanager::v5::BlobsInfos* response) override;

    //
    // NetworkService::Service GRPC service methods
    //

    grpc::Status GetNodeNetworkParams(grpc::ServerContext*     context,
        const servicemanager::v5::GetNodeNetworkParamsRequest* request,
        servicemanager::v5::GetNodeNetworkParamsResponse*      response) override;

    grpc::Status AllocateInstanceNetwork(grpc::ServerContext*     context,
        const servicemanager::v5::AllocateInstanceNetworkRequest* request,
        servicemanager::v5::AllocateInstanceNetworkResponse*      response) override;

    grpc::Status ReleaseInstanceNetwork(grpc::ServerContext*     context,
        const servicemanager::v5::ReleaseInstanceNetworkRequest* request,
        servicemanager::v5::ReleaseInstanceNetworkResponse*      response) override;

    grpc::Status ReleaseNodeNetwork(grpc::ServerContext*     context,
        const servicemanager::v5::ReleaseNodeNetworkRequest* request,
        servicemanager::v5::ReleaseNodeNetworkResponse*      response) override;

    grpc::Status SubscribeInstanceNetworkUpdates(grpc::ServerContext*              context,
        const servicemanager::v5::SubscribeInstanceNetworkUpdatesRequest*          request,
        grpc::ServerWriter<servicemanager::v5::InstanceNetworkUpdateNotification>* writer) override;

    grpc::Status SyncNetworkState(grpc::ServerContext*     context,
        const servicemanager::v5::SyncNetworkStateRequest* request,
        servicemanager::v5::SyncNetworkStateResponse*      response) override;

    // iamclient::CertListenerItf interface
    void OnCertChanged(const CertInfo& info) override;

    // NodeConnectionStatusListenerItf interface
    void OnNodeConnected(const String& nodeID) override;
    void OnNodeDisconnected(const String& nodeID) override;

    static constexpr auto cStreamPollInterval = std::chrono::seconds(1);

    SMHandler* FindNode(const String& nodeID);

    Error                     CreateServerCredentials();
    RetWithError<std::string> CorrectAddress(const std::string& addr) const;
    Error                     StartServer();
    Error                     StopServer();
    Error                     RestartServer();
    void                      ScheduleRestart();
    void                      OnRestartTimer();

    Config                                   mConfig {};
    cloudconnection::CloudConnectionItf*     mCloudConnection {};
    crypto::CertLoaderItf*                   mCertLoader {};
    crypto::x509::ProviderItf*               mCryptoProvider {};
    aos::iamclient::CertProviderItf*         mCertProvider {};
    imagemanager::ItemInfoProviderItf*       mItemInfoProvider {};
    alerts::ReceiverItf*                     mAlertsReceiver {};
    SenderItf*                               mLogSender {};
    launcher::SenderItf*                     mEnvVarsStatusSender {};
    monitoring::ReceiverItf*                 mMonitoringReceiver {};
    launcher::InstanceStatusReceiverItf*     mInstanceStatusReceiver {};
    nodeinfoprovider::SMInfoReceiverItf*     mSMInfoReceiver {};
    aos::networkmanager::NetworkProviderItf* mNetworkProvider {};
    bool                                     mInsecureConn {};

    std::unique_ptr<grpc::Server>            mServer;
    std::mutex                               mMutex;
    std::condition_variable                  mAllNodesDisconnectedCV;
    std::shared_ptr<grpc::ServerCredentials> mCredentials;

    std::vector<std::shared_ptr<SMHandler>> mSMHandlers;

    aos::Timer mReconnectTimer {};

    // Stream writers for SubscribeInstanceNetworkUpdates per nodeID
    struct NetworkUpdateStream {
        grpc::ServerContext*                                                       mContext {};
        grpc::ServerWriter<servicemanager::v5::InstanceNetworkUpdateNotification>* mWriter {};
    };

    std::mutex                                           mStreamMutex;
    std::condition_variable                              mStreamCV;
    std::unordered_map<std::string, NetworkUpdateStream> mNetworkUpdateStreams;
};

} // namespace aos::cm::smcontroller

#endif
