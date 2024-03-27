/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMCLIENT_HPP_
#define IAMCLIENT_HPP_

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>

#include <aos/common/cryptoutils.hpp>
#include <aos/common/tools/error.hpp>
#include <aos/iam/certhandler.hpp>

#include <iamanager.grpc.pb.h>

#include "config/config.hpp"
#include "iam/remoteiamhandler.hpp"

using ProvisioningService        = iamanager::v4::IAMProvisioningService;
using ProvisioningServiceStubPtr = std::unique_ptr<ProvisioningService::StubInterface>;
using CertificateService         = iamanager::v4::IAMCertificateService;
using CertificateServiceStubPtr  = std::unique_ptr<CertificateService::StubInterface>;

struct ConnectionDetails {
    RemoteIAM                      mRemoteIAMConfig;
    std::shared_ptr<grpc::Channel> mChannel;
};

class IAMClient : public RemoteIAMHandlerItf {
public:
    /**
     * Initializes IAM client instance.
     *
     * @param config client configuration.
     * @param certHandler certificate handler.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param provisioningMode flag indicating whether provisioning mode is active.
     * @returns aos::Error.
     */
    aos::Error Init(const Config& config, aos::iam::certhandler::CertHandlerItf& certHandler,
        aos::cryptoutils::CertLoaderItf& certLoader, aos::crypto::x509::ProviderItf& cryptoProvider,
        bool provisioningMode);

    /**
     * Returns remote node identifiers.
     *
     * @result aos::Array<aos::StaticString<aos::cNodeIDLen>>
     */
    aos::Array<aos::StaticString<aos::cNodeIDLen>> GetRemoteNodes() override;

    /**
     * Returns IAM cert types.
     *
     * @param nodeID node id.
     * @param[out] certTypes result certificate types.
     * @returns aos::Error.
     */
    aos::Error GetCertTypes(const aos::String&                              nodeID,
        aos::Array<aos::StaticString<aos::iam::certhandler::cCertTypeLen>>& certTypes) override;

    /**
     * Owns security storage.
     *
     * @param nodeID node id.
     * @param certType certificate type.
     * @param password owner password.
     * @returns aos::Error.
     */
    aos::Error SetOwner(const aos::String& nodeID, const aos::String& certType, const aos::String& password) override;

    /**
     * Clears security storage.
     *
     * @param nodeID node id.
     * @param certType certificate type.
     * @returns aos::Error.
     */
    aos::Error Clear(const aos::String& nodeID, const aos::String& certType) override;

    /**
     * Creates key pair.
     *
     * @param nodeID node id.
     * @param certType certificate type.
     * @param subjectCommonName common name of the subject.
     * @param password owner password.
     * @param[out] pemCSR certificate signing request in PEM.
     * @returns aos::Error.
     */
    aos::Error CreateKey(const aos::String& nodeID, const aos::String& certType, const aos::String& subjectCommonName,
        const aos::String& password, aos::String& pemCSR) override;

    /**
     * Applies certificate.
     *
     * @param nodeID node id.
     * @param certType certificate type.
     * @param pemCert certificate in a pem format.
     * @param[out] info result certificate information.
     * @returns aos::Error.
     */
    aos::Error ApplyCertificate(const aos::String& nodeID, const aos::String& certType, const aos::String& pemCert,
        aos::iam::certhandler::CertInfo& info) override;

    /**
     * Encrypts disk for a node.
     *
     * @param nodeID node identifier.
     * @param password password.
     * @returns aos::Error.
     */
    aos::Error EncryptDisk(const aos::String& nodeID, const aos::String& password) override;

    /**
     * Finishes provisioning.
     *
     * @param nodeID node identifier.
     * @returns aos::Error.
     */
    aos::Error FinishProvisioning(const aos::String& nodeID) override;

protected:
    virtual CertificateServiceStubPtr  CreateIAMCertificateServiceStub(const aos::String& nodeId);
    virtual ProvisioningServiceStubPtr CreateIAMProvisioningServiceStub(const aos::String& nodeId);

private:
    static constexpr size_t cMaxNodes {2};

    std::mutex                                mMutex;
    std::map<std::string, ConnectionDetails>  mRemoteIMs;
    std::shared_ptr<grpc::ChannelCredentials> mCredetials;

    void SetClientContext(grpc::ClientContext& context, const aos::String& nodeId);
};

#endif
