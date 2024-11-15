/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMSERVER_HPP_
#define IAMSERVER_HPP_

#include <string>
#include <thread>
#include <vector>

#include <grpcpp/server_builder.h>

#include <aos/common/crypto/utils.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/identhandler.hpp>
#include <aos/iam/nodeinfoprovider.hpp>
#include <aos/iam/permhandler.hpp>
#include <aos/iam/provisionmanager.hpp>
#include <config/config.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include "protectedmessagehandler.hpp"
#include "publicmessagehandler.hpp"

/**
 * IAM GRPC server
 */
class IAMServer : public aos::iam::nodemanager::NodeInfoListenerItf,
                  public aos::iam::identhandler::SubjectsObserverItf,
                  public aos::iam::provisionmanager::ProvisionManagerCallbackItf,
                  private aos::iam::certhandler::CertReceiverItf {
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
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param nodeInfoProvider node info provider.
     * @param nodeManager node manager.
     * @param provisionManager provision manager.
     * @param provisioningMode flag indicating whether provisioning mode is active.
     */
    aos::Error Init(const Config& config, aos::iam::certhandler::CertHandlerItf& certHandler,
        aos::iam::identhandler::IdentHandlerItf& identHandler, aos::iam::permhandler::PermHandlerItf& permHandler,
        aos::crypto::CertLoader& certLoader, aos::crypto::x509::ProviderItf& cryptoProvider,
        aos::iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
        aos::iam::nodemanager::NodeManagerItf&           nodeManager,
        aos::iam::provisionmanager::ProvisionManagerItf& provisionManager, bool provisioningMode);

    /**
     * Called when provisioning starts.
     *
     * @param password password.
     * @returns Error.
     */
    aos::Error OnStartProvisioning(const aos::String& password) override;

    /**
     * Called when provisioning finishes.
     *
     * @param password password.
     * @returns Error.
     */
    aos::Error OnFinishProvisioning(const aos::String& password) override;

    /**
     * Called on deprovisioning.
     *
     * @param password password.
     * @returns Error.
     */
    aos::Error OnDeprovision(const aos::String& password) override;

    /**
     * Called on disk encryption.
     *
     * @param password password.
     * @returns Error.
     */
    aos::Error OnEncryptDisk(const aos::String& password) override;

    /**
     * Node info change notification.
     *
     * @param info node info.
     */
    void OnNodeInfoChange(const aos::NodeInfo& info) override;

    /**
     * Node info removed notification.
     *
     * @param id id of the node been removed.
     */
    void OnNodeRemoved(const aos::String& id) override;

    /**
     * Destroys IAM server.
     */
    virtual ~IAMServer();

private:
    // identhandler::SubjectsObserverItf interface
    aos::Error SubjectsChanged(const aos::Array<aos::StaticString<aos::cSubjectIDLen>>& messages) override;

    // certhandler::CertReceiverItf interface
    void OnCertChanged(const aos::iam::certhandler::CertInfo& info) override;

    // lifecycle routines
    void Start();
    void Shutdown();

    // creating routines
    void CreatePublicServer(const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials);
    void CreateProtectedServer(const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials);

    Config                          mConfig;
    aos::crypto::CertLoader*        mCertLoader;
    aos::crypto::x509::ProviderItf* mCryptoProvider;

    NodeController                           mNodeController;
    PublicMessageHandler                     mPublicMessageHandler;
    ProtectedMessageHandler                  mProtectedMessageHandler;
    std::unique_ptr<grpc::Server>            mPublicServer, mProtectedServer;
    std::shared_ptr<grpc::ServerCredentials> mPublicCred, mProtectedCred;

    bool              mIsStarted = false;
    std::future<void> mCertChangedResult;
};

#endif
