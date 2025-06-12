/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMSERVER_HPP_
#define IAMSERVER_HPP_

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/server_builder.h>

#include <aos/common/crypto/utils.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/certprovider.hpp>
#include <aos/iam/identhandler.hpp>
#include <aos/iam/nodeinfoprovider.hpp>
#include <aos/iam/permhandler.hpp>
#include <aos/iam/provisionmanager.hpp>
#include <config/config.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include "protectedmessagehandler.hpp"
#include "publicmessagehandler.hpp"

namespace aos::iam::iamserver {

/**
 * IAM GRPC server
 */
class IAMServer : public nodemanager::NodeInfoListenerItf,
                  public identhandler::SubjectsObserverItf,
                  public provisionmanager::ProvisionManagerCallbackItf,
                  private certhandler::CertReceiverItf {
public:
    /**
     * Constructor.
     */
    IAMServer() = default;

    /**
     * Initializes IAM server instance.
     *
     * @param config server configuration.
     * @param certHandler certificate handler.
     * @param identHandler identification handler.
     * @param permHandler permission handler.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param nodeInfoProvider node info provider.
     * @param nodeManager node manager.
     * @param cryptoProvider crypto provider.
     * @param provisionManager provision manager.
     * @param provisioningMode flag indicating whether provisioning mode is active.
     */
    Error Init(const config::IAMServerConfig& config, certhandler::CertHandlerItf& certHandler,
        identhandler::IdentHandlerItf& identHandler, permhandler::PermHandlerItf& permHandler,
        crypto::CertLoader& certLoader, crypto::x509::ProviderItf& cryptoProvider,
        nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider, nodemanager::NodeManagerItf& nodeManager,
        certprovider::CertProviderItf& certProvider, provisionmanager::ProvisionManagerItf& provisionManager,
        bool provisioningMode);

    /**
     * Starts IAM server.
     *
     * @returns Error.
     */
    Error Start();

    /**
     * Stops IAM server.
     *
     * @returns Error.
     */
    Error Stop();

    /**
     * Called when provisioning starts.
     *
     * @param password password.
     * @returns Error.
     */
    Error OnStartProvisioning(const String& password) override;

    /**
     * Called when provisioning finishes.
     *
     * @param password password.
     * @returns Error.
     */
    Error OnFinishProvisioning(const String& password) override;

    /**
     * Called on deprovisioning.
     *
     * @param password password.
     * @returns Error.
     */
    Error OnDeprovision(const String& password) override;

    /**
     * Called on disk encryption.
     *
     * @param password password.
     * @returns Error.
     */
    Error OnEncryptDisk(const String& password) override;

    /**
     * Node info change notification.
     *
     * @param info node info.
     */
    void OnNodeInfoChange(const NodeInfo& info) override;

    /**
     * Node info removed notification.
     *
     * @param id id of the node been removed.
     */
    void OnNodeRemoved(const String& id) override;

private:
    // identhandler::SubjectsObserverItf interface
    Error SubjectsChanged(const Array<StaticString<cSubjectIDLen>>& messages) override;

    // certhandler::CertReceiverItf interface
    void OnCertChanged(const certhandler::CertInfo& info) override;

    // creating routines
    void CreatePublicServer(const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials);
    void CreateProtectedServer(const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials);

    config::IAMServerConfig      mConfig         = {};
    crypto::CertLoader*          mCertLoader     = nullptr;
    crypto::x509::ProviderItf*   mCryptoProvider = nullptr;
    certhandler::CertHandlerItf* mCertHandler    = nullptr;

    NodeController                           mNodeController;
    PublicMessageHandler                     mPublicMessageHandler;
    ProtectedMessageHandler                  mProtectedMessageHandler;
    std::unique_ptr<grpc::Server>            mPublicServer, mProtectedServer;
    std::shared_ptr<grpc::ServerCredentials> mPublicCred, mProtectedCred;

    std::atomic<bool> mIsStarted = false;
    std::future<void> mCertChangedResult;

    bool mProvisioningMode {};
};

} // namespace aos::iam::iamserver

#endif
