/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_SMCONTROLLER_HPP_
#define AOS_CM_SMCONTROLLER_SMCONTROLLER_HPP_

#include <condition_variable>
#include <memory>
#include <mutex>

#include <grpcpp/server.h>
#include <servicemanager/v5/servicemanager.grpc.pb.h>

#include <core/cm/imagemanager/itf/blobinfoprovider.hpp>
#include <core/cm/launcher/itf/envvarhandler.hpp>
#include <core/cm/launcher/itf/instancerunner.hpp>
#include <core/cm/launcher/itf/monitoringprovider.hpp>
#include <core/cm/networkmanager/itf/nodenetwork.hpp>
#include <core/cm/nodeinfoprovider/itf/sminforeceiver.hpp>
#include <core/cm/smcontroller/itf/logprovider.hpp>
#include <core/cm/smcontroller/itf/smcontroller.hpp>
#include <core/cm/unitconfig/itf/nodeconfighandler.hpp>
#include <core/common/cloudconnection/itf/cloudconnection.hpp>
#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>

#include "config.hpp"
#include "smhandler.hpp"

namespace aos::cm::smcontroller {

/**
 * Service Manager Controller.
 */
class SMController : public SMControllerItf,
                     private cloudconnection::ConnectionListenerItf,
                     private NodeConnectionStatusListenerItf,
                     private iamclient::CertListenerItf,
                     private servicemanager::v5::SMService::Service {
public:
    /**
     * Initializes the SM controller.
     *
     * @param config configuration.
     * @param cloudConnection cloud connection.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param blobInfoProvider blob info provider.
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
        iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
        crypto::x509::ProviderItf& cryptoProvider, imagemanager::BlobInfoProviderItf& blobInfoProvider,
        alerts::ReceiverItf& alertsReceiver, SenderItf& logSender, launcher::SenderItf& envVarsStatusSender,
        monitoring::ReceiverItf& monitoringReceiver, launcher::InstanceStatusReceiverItf& instanceStatusReceiver,
        nodeinfoprovider::SMInfoReceiverItf& smInfoReceiver, bool insecureConn = false);

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
    // NodeNetworkItf interface methods
    //

    /**
     * Updates network parameters for a node.
     *
     * @param nodeID Node ID.
     * @param networkParameters Network parameters.
     * @return Error.
     */
    Error UpdateNetworks(const String& nodeID, const Array<UpdateNetworkParameters>& networkParameters) override;

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

private:
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

    // iamclient::CertListenerItf interface
    void OnCertChanged(const CertInfo& info) override;

    // NodeConnectionStatusListenerItf interface
    void OnNodeConnected(const String& nodeID) override;
    void OnNodeDisconnected(const String& nodeID) override;

    SMHandler* FindNode(const String& nodeID);

    Error                     CreateServerCredentials();
    RetWithError<std::string> CorrectAddress(const std::string& addr) const;
    Error                     StartServer();
    Error                     StopServer();

    Config                               mConfig {};
    cloudconnection::CloudConnectionItf* mCloudConnection {};
    crypto::CertLoaderItf*               mCertLoader {};
    crypto::x509::ProviderItf*           mCryptoProvider {};
    iamclient::CertProviderItf*          mCertProvider {};
    imagemanager::BlobInfoProviderItf*   mBlobInfoProvider {};
    alerts::ReceiverItf*                 mAlertsReceiver {};
    SenderItf*                           mLogSender {};
    launcher::SenderItf*                 mEnvVarsStatusSender {};
    monitoring::ReceiverItf*             mMonitoringReceiver {};
    launcher::InstanceStatusReceiverItf* mInstanceStatusReceiver {};
    nodeinfoprovider::SMInfoReceiverItf* mSMInfoReceiver {};
    bool                                 mInsecureConn {};

    std::unique_ptr<grpc::Server>            mServer;
    std::mutex                               mMutex;
    std::condition_variable                  mAllNodesDisconnectedCV;
    std::shared_ptr<grpc::ServerCredentials> mCredentials;

    std::vector<std::shared_ptr<SMHandler>> mSMHandlers;
};

} // namespace aos::cm::smcontroller

#endif
