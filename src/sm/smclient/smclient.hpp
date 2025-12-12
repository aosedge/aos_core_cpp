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

#include <servicemanager/v5/servicemanager.grpc.pb.h>

#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/instancestatusprovider/itf/instancestatusprovider.hpp>
#include <core/common/monitoring/itf/monitoring.hpp>
#include <core/common/nodeconfig/itf/jsonprovider.hpp>
#include <core/common/tools/error.hpp>
#include <core/common/types/instance.hpp>
#include <core/sm/launcher/itf/launcher.hpp>
#include <core/sm/launcher/itf/runtimeinfoprovider.hpp>
#include <core/sm/logging/itf/logprovider.hpp>
#include <core/sm/networkmanager/itf/networkmanager.hpp>
#include <core/sm/nodeconfig/itf/nodeconfighandler.hpp>
#include <core/sm/resourcemanager/itf/resourceinfoprovider.hpp>
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
     * @param networkManager network manager.
     * @param monitoring monitoring.
     * @param instanceStatusProvider instance status provider.
     * @param jsonProvider JSON provider.
     * @param secureConnection secure connection flag.
     * @return Error.
     */
    Error Init(const Config& config, const std::string& nodeID,
        aos::common::iamclient::TLSCredentialsItf& tlsCredentials, aos::iamclient::CertProviderItf& certProvider,
        launcher::RuntimeInfoProviderItf&         runtimeInfoProvider,
        resourcemanager::ResourceInfoProviderItf& resourceInfoProvider,
        nodeconfig::NodeConfigHandlerItf& nodeConfigHandler, launcher::LauncherItf& launcher,
        logging::LogProviderItf& logProvider, networkmanager::NetworkManagerItf& networkManager,
        aos::monitoring::MonitoringItf& monitoring, aos::instancestatusprovider::ProviderItf& instanceStatusProvider,
        aos::nodeconfig::JSONProviderItf& jsonProvider, bool secureConnection = true);

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

    /**
     * Destroys object instance.
     */
    ~SMClient() = default;

private:
    using StubPtr = std::unique_ptr<smproto::SMService::StubInterface>;
    using StreamPtr
        = std::unique_ptr<grpc::ClientReaderWriterInterface<smproto::SMOutgoingMessages, smproto::SMIncomingMessages>>;

    std::unique_ptr<grpc::ClientContext> CreateClientContext();
    StubPtr CreateStub(const std::string& url, const std::shared_ptr<grpc::ChannelCredentials>& credentials);

    bool SendSMInfo();
    bool SendNodeInstancesStatus();

    bool RegisterSM(const std::string& url);
    void ConnectionLoop() noexcept;
    void HandleIncomingMessages();

    Error ProcessGetNodeConfigStatus();
    Error ProcessCheckNodeConfig(const smproto::CheckNodeConfig& checkConfig);
    Error ProcessSetNodeConfig(const smproto::SetNodeConfig& setConfig);
    Error ProcessUpdateInstances(const smproto::UpdateInstances& updateInstances);
    Error ProcessSystemLogRequest(const smproto::SystemLogRequest& request);
    Error ProcessInstanceLogRequest(const smproto::InstanceLogRequest& request);
    Error ProcessInstanceCrashLogRequest(const smproto::InstanceCrashLogRequest& request);
    Error ProcessGetAverageMonitoring();
    Error ProcessConnectionStatus(const smproto::ConnectionStatus& status);
    Error ProcessUpdateNetworks(const smproto::UpdateNetworks& updateNetworks);

    Config                                     mConfig {};
    std::string                                mNodeID;
    aos::common::iamclient::TLSCredentialsItf* mTLSCredentials {};
    aos::iamclient::CertProviderItf*           mCertProvider {};
    launcher::RuntimeInfoProviderItf*          mRuntimeInfoProvider {};
    resourcemanager::ResourceInfoProviderItf*  mResourceInfoProvider {};
    nodeconfig::NodeConfigHandlerItf*          mNodeConfigHandler {};
    launcher::LauncherItf*                     mLauncher {};
    logging::LogProviderItf*                   mLogProvider {};
    networkmanager::NetworkManagerItf*         mNetworkManager {};
    aos::monitoring::MonitoringItf*            mMonitoring {};
    aos::instancestatusprovider::ProviderItf*  mInstanceStatusProvider {};
    aos::nodeconfig::JSONProviderItf*          mJSONProvider {};
    bool                                       mSecureConnection {};
    std::shared_ptr<grpc::ChannelCredentials>  mCredentials;

    std::unique_ptr<grpc::ClientContext> mCtx;
    StreamPtr                            mStream;
    StubPtr                              mStub;

    std::thread             mConnectionThread;
    std::mutex              mMutex;
    bool                    mStopped = true;
    std::condition_variable mStoppedCV;

    std::vector<aos::cloudconnection::ConnectionListenerItf*> mConnectionListeners;
};

} // namespace aos::sm::smclient

#endif
