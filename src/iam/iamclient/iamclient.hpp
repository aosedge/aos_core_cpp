/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_IAMCLIENT_IAMCLIENT_HPP_
#define AOS_IAM_IAMCLIENT_IAMCLIENT_HPP_

#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/crypto/itf/crypto.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/iamclient/itf/identprovider.hpp>
#include <core/common/tools/error.hpp>
#include <core/iam/currentnode/itf/currentnodehandler.hpp>
#include <core/iam/provisionmanager/provisionmanager.hpp>

#include <common/iamclient/publicnodeservice.hpp>

#include <iam/config/config.hpp>

namespace aos::iam::iamclient {

/**
 * GRPC IAM client.
 */
class IAMClient : public common::iamclient::PublicNodesService, private aos::iamclient::CertListenerItf {
public:
    /**
     * Initializes IAM client instance.
     *
     * @param config client configuration.
     * @param identProvider identification provider.
     * @param certProvider certificate provider.
     * @param provisionManager provision manager.
     * @param tlsCredentials TLS credentials.
     * @param currentNodeHandler current node handler.
     * @param provisioningMode flag indicating whether provisioning mode is active.
     * @returns Error.
     */
    Error Init(const config::IAMClientConfig& config, aos::iamclient::IdentProviderItf* identProvider,
        aos::iamclient::CertProviderItf& certProvider, provisionmanager::ProvisionManagerItf& provisionManager,
        common::iamclient::TLSCredentialsItf& tlsCredentials, currentnode::CurrentNodeHandlerItf& currentNodeHandler,
        bool provisioningMode);

    /**
     * Starts IAM client.
     *
     * @returns Error.
     */
    Error Start();

    /**
     * Stops IAM client.
     *
     * @returns Error.
     */
    Error Stop();

protected:
    Error ReceiveMessage(const iamanager::v6::IAMIncomingMessages& msg) override;
    void  OnConnected() override;
    void  OnDisconnected() override;

private:
    void OnCertChanged(const CertInfo& info) override;

    Error SendNodeInfo();
    Error ProcessStartProvisioning(const iamanager::v6::StartProvisioningRequest& request);
    Error ProcessFinishProvisioning(const iamanager::v6::FinishProvisioningRequest& request);
    Error ProcessDeprovision(const iamanager::v6::DeprovisionRequest& request);
    Error ProcessPauseNode(const iamanager::v6::PauseNodeRequest& request);
    Error ProcessResumeNode(const iamanager::v6::ResumeNodeRequest& request);
    Error ProcessCreateKey(const iamanager::v6::CreateKeyRequest& request);
    Error ProcessApplyCert(const iamanager::v6::ApplyCertRequest& request);
    Error ProcessGetCertTypes(const iamanager::v6::GetCertTypesRequest& request);

    Error CheckCurrentNodeState(const std::optional<std::initializer_list<NodeState>>& allowedStates);

    Error SendCreateKeyResponse(const String& nodeID, const String& type, const String& csr, const Error& error);
    Error SendApplyCertResponse(const String& nodeID, const String& type, const String& certURL,
        const Array<uint8_t>& serial, const Error& error);
    Error SendGetCertTypesResponse(const provisionmanager::CertTypes& types, const Error& error);

    aos::iamclient::IdentProviderItf*      mIdentProvider      = nullptr;
    provisionmanager::ProvisionManagerItf* mProvisionManager   = nullptr;
    aos::iamclient::CertProviderItf*       mCertProvider       = nullptr;
    currentnode::CurrentNodeHandlerItf*    mCurrentNodeHandler = nullptr;

    std::string mCertStorage;
};

} // namespace aos::iam::iamclient

#endif
