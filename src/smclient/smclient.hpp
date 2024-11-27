/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SMCLIENT_HPP_
#define SMCLIENT_HPP_

#include <condition_variable>
#include <thread>

#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>

#include <aos/common/alerts/alerts.hpp>
#include <aos/common/cloudprotocol/alerts.hpp>
#include <aos/common/crypto/crypto.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/common/monitoring/monitoring.hpp>
#include <aos/common/tools/error.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/nodeinfoprovider.hpp>
#include <aos/iam/provisionmanager.hpp>
#include <aos/sm/launcher.hpp>
#include <aos/sm/logprovider.hpp>
#include <aos/sm/networkmanager.hpp>
#include <aos/sm/resourcemanager.hpp>

#include <servicemanager/v4/servicemanager.grpc.pb.h>

#include "config/config.hpp"

namespace smproto = servicemanager::v4;

namespace aos::sm::client {

using PublicNodeService = smproto::SMService;

/**
 * GRPC service manager client.
 */
class SMClient : public iam::certhandler::CertReceiverItf,
                 public monitoring::SenderItf,
                 public alerts::SenderItf,
                 public sm::logprovider::LogObserverItf,
                 public sm::launcher::InstanceStatusReceiverItf,
                 private NonCopyable {
public:
    /**
     * Initializes SM client instance.
     *
     * @param config client configuration.
     * @param provisionManager provision manager.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param nodeInfoProvider node info provider.
     * @param resourceManager resource manager.
     * @param networkManager network manager.
     * @param logProvider log provider.
     * @param resourceMonitor resource monitor.
     * @param launcher launcher.
     * @param secureConnection flag indicating whether connection is secured.
     * @returns Error.
     */
    Error Init(const config::Config& config, iam::provisionmanager::ProvisionManagerItf& provisionManager,
        crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
        iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
        sm::resourcemanager::ResourceManagerItf& resourceManager, sm::networkmanager::NetworkManagerItf& networkManager,
        sm::logprovider::LogProviderItf& logProvider, monitoring::ResourceMonitorItf& resourceMonitor,
        sm::launcher::LauncherItf& launcher, bool secureConnection = true);

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

    /**
     * Processes certificate updates.
     *
     * @param info certificate info.
     */
    void OnCertChanged(const iam::certhandler::CertInfo& info) override;

    /**
     * Sends monitoring data.
     *
     * @param monitoringData monitoring data.
     * @return Error.
     */
    Error SendMonitoringData(const monitoring::NodeMonitoringData& monitoringData) override;

    /**
     * Sends alert data.
     *
     * @param alert alert variant.
     * @return Error.
     */
    Error SendAlert(const cloudprotocol::AlertVariant& alert) override;

    /**
     * On log received event handler.
     *
     * @param log log.
     * @return Error.
     */
    Error OnLogReceived(const cloudprotocol::PushLog& log) override;

    /**
     * Sends instances run status.
     *
     * @param instances instances status array.
     * @return Error.
     */
    Error InstancesRunStatus(const Array<InstanceStatus>& instances) override;

    /**
     * Sends instances update status.
     * @param instances instances status array.
     *
     * @return Error.
     */
    Error InstancesUpdateStatus(const Array<InstanceStatus>& instances) override;

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

    bool SendNodeConfigStatus(const String& version, const Error& configErr);

    bool RegisterSM(const std::string& url);
    void ConnectionLoop() noexcept;
    void HandleIncomingMessages() noexcept;

    bool ProcessGetNodeConfigStatus();
    bool ProcessCheckNodeConfig(const smproto::CheckNodeConfig& request);
    bool ProcessSetNodeConfig(const smproto::SetNodeConfig& request);
    bool ProcessRunInstances(const smproto::RunInstances& request);
    bool ProcessUpdateNetworks(const smproto::UpdateNetworks& request);
    bool ProcessGetSystemLogRequest(const smproto::SystemLogRequest& request);
    bool ProcessGetInstanceLogRequest(const smproto::InstanceLogRequest& request);
    bool ProcessGetInstanceCrashLogRequest(const smproto::InstanceCrashLogRequest& request);
    bool ProcessOverrideEnvVars(const smproto::OverrideEnvVars& request);
    bool ProcessGetAverageMonitoring();
    bool ProcessConnectionStatus(const smproto::ConnectionStatus& request);

    config::Config                              mConfig           = {};
    iam::provisionmanager::ProvisionManagerItf* mProvisionManager = nullptr;
    crypto::CertLoaderItf*                      mCertLoader       = nullptr;
    crypto::x509::ProviderItf*                  mCryptoProvider   = nullptr;
    iam::nodeinfoprovider::NodeInfoProviderItf* mNodeInfoProvider = nullptr;
    sm::resourcemanager::ResourceManagerItf*    mResourceManager  = nullptr;
    sm::networkmanager::NetworkManagerItf*      mNetworkManager   = nullptr;
    sm::logprovider::LogProviderItf*            mLogProvider      = nullptr;
    monitoring::ResourceMonitorItf*             mResourceMonitor  = nullptr;
    sm::launcher::LauncherItf*                  mLauncher         = nullptr;

    std::vector<std::shared_ptr<grpc::ChannelCredentials>> mCredentialList;
    bool                                                   mCredentialListUpdated = false;
    bool                                                   mSecureConnection      = true;
    NodeInfo                                               mNodeInfo;

    std::unique_ptr<grpc::ClientContext> mCtx;
    StreamPtr                            mStream;
    StubPtr                              mStub;

    std::thread             mConnectionThread;
    std::mutex              mMutex;
    bool                    mStopped = true;
    std::condition_variable mStoppedCV;
};

} // namespace aos::sm::client

#endif
