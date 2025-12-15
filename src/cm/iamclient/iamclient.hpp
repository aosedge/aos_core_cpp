/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_IAMCLIENT_IAMCLIENT_HPP_
#define AOS_CM_IAMCLIENT_IAMCLIENT_HPP_

#include <common/iamclient/certificateservice.hpp>
#include <common/iamclient/nodesservice.hpp>
#include <common/iamclient/provisioningservice.hpp>
#include <common/iamclient/publiccertservice.hpp>
#include <common/iamclient/publiccurrentnodeservice.hpp>
#include <common/iamclient/publicidentityservice.hpp>
#include <common/iamclient/publicnodeservice.hpp>

#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/tools/error.hpp>

namespace aos::cm::iamclient {

/**
 * IAM client that aggregates IAM services used by IAMClientItf.
 */
class IAMClient : public aos::common::iamclient::CertificateService,
                  public aos::common::iamclient::NodesService,
                  public aos::common::iamclient::ProvisioningService,
                  public aos::common::iamclient::PublicCertService,
                  public aos::common::iamclient::PublicNodesService,
                  public aos::common::iamclient::PublicCurrentNodeService,
                  public aos::common::iamclient::PublicIdentityService,
                  public aos::iamclient::CertListenerItf {
public:
    /**
     * Destructor.
     */
    ~IAMClient();

    /**
     * Initializes IAM client.
     *
     * @param iamProtectedServerURL IAM protected server URL.
     * @param iamPublicServerURL IAM public server URL.
     * @param certStorage certificate storage.
     * @param tlsCredentials TLS credentials.
     * @param certType certificate type to subscribe for updates.
     * @param insecureConnection use insecure connection.
     * @return Error.
     */
    Error Init(const std::string& iamProtectedServerURL, const std::string& iamPublicServerURL,
        const std::string& certStorage, aos::common::iamclient::TLSCredentialsItf& tlsCredentials,
        const String& certType, bool insecureConnection = false);

    /**
     * Reconnects all services.
     *
     * @param info certificate info.
     */
    void OnCertChanged(const CertInfo& info) override;
};

} // namespace aos::cm::iamclient

#endif
