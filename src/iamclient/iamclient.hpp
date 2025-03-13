/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMCLIENT_HPP_
#define IAMCLIENT_HPP_

#include <condition_variable>
#include <thread>

#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>

#include <aos/common/crypto/crypto.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/common/tools/error.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/certprovider.hpp>
#include <aos/iam/identhandler.hpp>
#include <aos/iam/nodeinfoprovider.hpp>
#include <aos/iam/provisionmanager.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include "config/config.hpp"

namespace aos::iam::iamclient {

using PublicNodeService        = iamanager::v5::IAMPublicNodesService;
using PublicNodeServiceStubPtr = std::unique_ptr<PublicNodeService::StubInterface>;

/**
 * GRPC IAM client.
 */
class IAMClient : private certhandler::CertReceiverItf {
public:
    /**
     * Initializes IAM client instance.
     *
     * @param config client configuration.
     * @param identHandler identification handler.
     * @param certProvider certificate provider.
     * @param provisionManager provision manager.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param nodeInfoProvider node info provider.
     * @param provisioningMode flag indicating whether provisioning mode is active.
     * @returns Error.
     */
    Error Init(const config::IAMClientConfig& config, identhandler::IdentHandlerItf* identHandler,
        certprovider::CertProviderItf& certProvider, provisionmanager::ProvisionManagerItf& provisionManager,
        crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
        nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider, bool provisioningMode);

    /**
     * Destroys object instance.
     */
    ~IAMClient();

private:
    void OnCertChanged(const certhandler::CertInfo& info) override;

    using StreamPtr = std::unique_ptr<
        grpc::ClientReaderWriterInterface<iamanager::v5::IAMOutgoingMessages, iamanager::v5::IAMIncomingMessages>>;

    std::unique_ptr<grpc::ClientContext> CreateClientContext();
    PublicNodeServiceStubPtr             CreateStub(
                    const std::string& url, const std::shared_ptr<grpc::ChannelCredentials>& credentials);

    bool RegisterNode(const std::string& url);

    void ConnectionLoop() noexcept;
    void HandleIncomingMessages() noexcept;

    bool SendNodeInfo();
    bool ProcessStartProvisioning(const iamanager::v5::StartProvisioningRequest& request);
    bool ProcessFinishProvisioning(const iamanager::v5::FinishProvisioningRequest& request);
    bool ProcessDeprovision(const iamanager::v5::DeprovisionRequest& request);
    bool ProcessPauseNode(const iamanager::v5::PauseNodeRequest& request);
    bool ProcessResumeNode(const iamanager::v5::ResumeNodeRequest& request);
    bool ProcessCreateKey(const iamanager::v5::CreateKeyRequest& request);
    bool ProcessApplyCert(const iamanager::v5::ApplyCertRequest& request);
    bool ProcessGetCertTypes(const iamanager::v5::GetCertTypesRequest& request);

    Error CheckCurrentNodeStatus(const std::initializer_list<NodeStatus>& allowedStatuses);

    bool SendCreateKeyResponse(const String& nodeID, const String& type, const String& csr, const Error& error);
    bool SendApplyCertResponse(const String& nodeID, const String& type, const String& certURL,
        const Array<uint8_t>& serial, const Error& error);
    bool SendGetCertTypesResponse(const provisionmanager::CertTypes& types, const Error& error);

    identhandler::IdentHandlerItf*         mIdentHandler     = nullptr;
    provisionmanager::ProvisionManagerItf* mProvisionManager = nullptr;
    certprovider::CertProviderItf*         mCertProvider     = nullptr;
    crypto::CertLoaderItf*                 mCertLoader       = nullptr;
    crypto::x509::ProviderItf*             mCryptoProvider   = nullptr;
    nodeinfoprovider::NodeInfoProviderItf* mNodeInfoProvider = nullptr;

    std::vector<std::shared_ptr<grpc::ChannelCredentials>> mCredentialList;
    bool                                                   mCredentialListUpdated = false;

    std::vector<std::string> mStartProvisioningCmdArgs;
    std::vector<std::string> mDiskEncryptionCmdArgs;
    std::vector<std::string> mFinishProvisioningCmdArgs;
    std::vector<std::string> mDeprovisionCmdArgs;
    Duration                 mReconnectInterval;
    std::string              mServerURL;
    std::string              mCACert;

    std::unique_ptr<grpc::ClientContext> mRegisterNodeCtx;
    StreamPtr                            mStream;
    PublicNodeServiceStubPtr             mPublicNodeServiceStub;

    std::thread             mConnectionThread;
    std::condition_variable mShutdownCV;
    bool                    mShutdown = false;
    std::mutex              mShutdownLock;
};

} // namespace aos::iam::iamclient

#endif
