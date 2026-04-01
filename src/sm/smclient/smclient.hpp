/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_SMCLIENT_SMCLIENT_HPP_
#define AOS_SM_SMCLIENT_SMCLIENT_HPP_

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>

#include <servicemanager/v5/network.grpc.pb.h>
#include <servicemanager/v5/servicemanager.grpc.pb.h>

#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/instancestatusprovider/itf/instancestatusprovider.hpp>
#include <core/common/monitoring/itf/monitoring.hpp>
#include <core/common/networkmanager/itf/networkprovider.hpp>
#include <core/common/nodeconfig/itf/jsonprovider.hpp>
#include <core/common/tools/error.hpp>
#include <core/common/types/instance.hpp>
#include <core/sm/launcher/itf/launcher.hpp>
#include <core/sm/launcher/itf/runtimeinfoprovider.hpp>
#include <core/sm/logging/itf/logprovider.hpp>
#include <core/sm/nodeconfig/itf/nodeconfighandler.hpp>
#include <core/sm/resourcemanager/itf/resourceinfoprovider.hpp>
#include <core/sm/smclient/itf/connection.hpp>
#include <core/sm/smclient/itf/smclient.hpp>

#include <common/iamclient/itf/tlscredentials.hpp>

#include "config.hpp"

namespace smproto = servicemanager::v5;

namespace aos::sm::smclient {

/**
 * GRPC service manager client.
 */
class SMClient : public SMClientItf, public aos::iamclient::CertListenerItf, private NonCopyable {
public:
    /**
     * Initializes SM client instance.
     *
     * @param config client configuration.
     * @param nodeID node ID.
     * @param tlsCredentials TLS credentials.
     * @param certProvider certificate provider.
     * @param runtimeInfoProvider runtime info provider.
     * @param resourceInfoProvider resource info provider.
     * @param nodeConfigHandler node config handler.
     * @param launcher launcher.
     * @param logProvider log provider.
     * @param monitoring monitoring.
     * @param instanceStatusProvider instance status provider.
     * @param jsonProvider JSON provider.
     * @param networkUpdateHandler handler for pending firewall update notifications.
     * @param secureConnection secure connection flag.
     * @return Error.
     */
    Error Init(const Config& config, const std::string& nodeID,
        aos::common::iamclient::TLSCredentialsItf& tlsCredentials, aos::iamclient::CertProviderItf& certProvider,
        launcher::RuntimeInfoProviderItf&         runtimeInfoProvider,
        resourcemanager::ResourceInfoProviderItf& resourceInfoProvider,
        nodeconfig::NodeConfigHandlerItf& nodeConfigHandler, launcher::LauncherItf& launcher,
        logging::LogProviderItf& logProvider, aos::monitoring::MonitoringItf& monitoring,
        aos::instancestatusprovider::ProviderItf&     instanceStatusProvider,
        aos::nodeconfig::JSONProviderItf&             jsonProvider,
        aos::networkmanager::PendingUpdateHandlerItf& networkUpdateHandler, bool secureConnection = true);

    /**
     * Starts the client.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops the client.
     *
     * @return Error.
     */
    Error Stop();

    // aos::iamclient::CertListenerItf interface

    /**
     * Processes certificate updates.
     *
     * @param info certificate info.
     */
    void OnCertChanged(const CertInfo& info) override;

    // aos::alerts::SenderItf interface

    /**
     * Sends alert data.
     *
     * @param alert alert variant.
     * @return Error.
     */
    Error SendAlert(const AlertVariant& alert) override;

    // aos::monitoring::SenderItf interface

    /**
     * Sends monitoring data.
     *
     * @param monitoringData monitoring data.
     * @return Error.
     */
    Error SendMonitoringData(const aos::monitoring::NodeMonitoringData& monitoringData) override;

    // aos::logging::SenderItf interface

    /**
     * Sends log.
     *
     * @param log log to send.
     * @return Error.
     */
    Error SendLog(const PushLog& log) override;

    // aos::sm::launcher::SenderItf interface

    /**
     * Sends node instances statuses.
     *
     * @param statuses instances statuses.
     * @return Error.
     */
    Error SendNodeInstancesStatuses(const Array<aos::InstanceStatus>& statuses) override;

    /**
     * Sends update instances statuses.
     *
     * @param statuses instances statuses.
     * @return Error.
     */
    Error SendUpdateInstancesStatuses(const Array<aos::InstanceStatus>& statuses) override;

    // aos::sm::imagemanager::BlobInfoProviderItf interface

    /**
     * Gets blobs info.
     *
     * @param digests list of blob digests.
     * @param[out] urls blobs URLs.
     * @return Error.
     */
    Error GetBlobsInfo(
        const Array<StaticString<oci::cDigestLen>>& digests, Array<StaticString<cURLLen>>& urls) override;

    // aos::cloudconnection::CloudConnectionItf interface

    /**
     * Subscribes to cloud connection events.
     *
     * @param listener listener reference.
     */
    Error SubscribeListener(aos::cloudconnection::ConnectionListenerItf& listener) override;

    /**
     * Unsubscribes from cloud connection events.
     *
     * @param listener listener reference.
     */
    Error UnsubscribeListener(aos::cloudconnection::ConnectionListenerItf& listener) override;

    // network::NetworkProviderItf interface

    /**
     * Gets node network parameters from CM.
     *
     * @param networkID network identifier.
     * @param nodeID node identifier.
     * @param[out] result node network parameters.
     * @return Error.
     */
    Error GetNodeNetworkParams(const String& networkID, const String& nodeID, NetworkParams& result) override;

    /**
     * Allocates instance network from CM.
     *
     * @param instance instance identifier.
     * @param networkID network identifier.
     * @param nodeID node identifier.
     * @param serviceData network service data.
     * @param[out] result instance network parameters.
     * @return Error.
     */
    Error AllocateInstanceNetwork(const InstanceIdent& instance, const String& networkID, const String& nodeID,
        const UpdateItemNetworkParams& serviceData, InstanceNetworkAllocation& result) override;

    /**
     * Releases instance network on CM.
     *
     * @param instance instance identifier.
     * @param nodeID node identifier.
     * @return Error.
     */
    Error ReleaseInstanceNetwork(const InstanceIdent& instance, const String& nodeID) override;

    /**
     * Releases node network on CM.
     *
     * @param networkID network identifier.
     * @param nodeID node identifier.
     * @return Error.
     */
    Error ReleaseNodeNetwork(const String& networkID, const String& nodeID) override;

    /**
     * Sends current instance network state to CM for reconciliation.
     *
     * @param nodeID node identifier.
     * @param instances array of instance network state info.
     * @return Error.
     */
    Error SyncNetworkState(const String& nodeID, const Array<InstanceNetworkStateInfo>& instances) override;

    // ConnectionItf interface

    /**
     * Subscribes to connection events.
     *
     * @param listener listener reference.
     * @return Error.
     */
    Error SubscribeListener(ConnectListenerItf& listener) override;

    /**
     * Unsubscribes from connection events.
     *
     * @param listener listener reference.
     * @return Error.
     */
    Error UnsubscribeListener(ConnectListenerItf& listener) override;

    /**
     * Checks if the connection is established.
     *
     * @return true if connected, false otherwise.
     */
    bool IsConnected() const override;

    /**
     * Destroys object instance.
     */
    ~SMClient() = default;

private:
    using StubPtr        = std::unique_ptr<smproto::SMService::StubInterface>;
    using NetworkStubPtr = std::unique_ptr<smproto::NetworkService::Stub>;
    using StreamPtr
        = std::unique_ptr<grpc::ClientReaderWriterInterface<smproto::SMOutgoingMessages, smproto::SMIncomingMessages>>;

    std::unique_ptr<grpc::ClientContext> CreateClientContext();
    Error                                CreateCredentials();

    bool SendSMInfo();
    bool SendNodeInstancesStatus();

    bool RegisterSM(const std::string& url);
    void ConnectionLoop() noexcept;
    void HandleIncomingMessages();
    void StartNetworkUpdateSubscription();

    Error ProcessGetNodeConfigStatus();
    Error ProcessCheckNodeConfig(const smproto::CheckNodeConfig& checkConfig);
    Error ProcessSetNodeConfig(const smproto::SetNodeConfig& setConfig);
    Error ProcessUpdateInstances(const smproto::UpdateInstances& updateInstances);
    Error ProcessSystemLogRequest(const smproto::SystemLogRequest& request);
    Error ProcessInstanceLogRequest(const smproto::InstanceLogRequest& request);
    Error ProcessInstanceCrashLogRequest(const smproto::InstanceCrashLogRequest& request);
    Error ProcessGetAverageMonitoring();
    Error ProcessConnectionStatus(const smproto::ConnectionStatus& status);

    Config                                     mConfig {};
    std::string                                mNodeID;
    aos::common::iamclient::TLSCredentialsItf* mTLSCredentials {};
    aos::iamclient::CertProviderItf*           mCertProvider {};
    launcher::RuntimeInfoProviderItf*          mRuntimeInfoProvider {};
    resourcemanager::ResourceInfoProviderItf*  mResourceInfoProvider {};
    nodeconfig::NodeConfigHandlerItf*          mNodeConfigHandler {};
    launcher::LauncherItf*                     mLauncher {};
    logging::LogProviderItf*                   mLogProvider {};
    aos::monitoring::MonitoringItf*            mMonitoring {};
    aos::instancestatusprovider::ProviderItf*  mInstanceStatusProvider {};
    aos::nodeconfig::JSONProviderItf*          mJSONProvider {};
    bool                                       mSecureConnection {};
    std::shared_ptr<grpc::ChannelCredentials>  mCredentials;

    std::unique_ptr<grpc::ClientContext> mCtx;
    StreamPtr                            mStream;
    StubPtr                              mStub;
    NetworkStubPtr                       mNetworkStub;

    std::thread                                       mConnectionThread;
    mutable std::mutex                                mMutex;
    bool                                              mStopped {true};
    std::optional<servicemanager::v5::ConnectionEnum> mConnectionStatus;
    std::condition_variable                           mStoppedCV;

    std::vector<aos::cloudconnection::ConnectionListenerItf*> mConnectionListeners;
    std::vector<aos::sm::smclient::ConnectListenerItf*>       mConnectListeners;

    // Network update stream
    aos::networkmanager::PendingUpdateHandlerItf* mNetworkUpdateHandler = nullptr;
    std::thread                                   mNetworkUpdateThread;
    std::condition_variable                       mNetworkUpdateCV;
    std::unique_ptr<grpc::ClientContext>          mNetworkUpdateCtx;
    std::unique_ptr<grpc::ClientReader<servicemanager::v5::InstanceNetworkUpdateNotification>> mNetworkUpdateReader;
};

} // namespace aos::sm::smclient

#endif
