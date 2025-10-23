/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_CERTIFICATESERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_CERTIFICATESERVICE_HPP_

#include <mutex>

#include <iamanager/v6/iamanager.grpc.pb.h>

#include <core/common/iamclient/itf/certhandler.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

/**
 * Certificate service.
 */
class CertificateService : public aos::iamclient::CertHandlerItf {
public:
    /**
     * Initializes certificate service.
     *
     * @param iamProtectedServerURL IAM protected server URL.
     * @param certStorage certificate storage.
     * @param tlsCredentials TLS credentials.
     * @param insecureConnection use insecure connection.
     * @return Error.
     */
    Error Init(const std::string& iamProtectedServerURL, const std::string& certStorage,
        TLSCredentialsItf& tlsCredentials, bool insecureConnection = false);

    /**
     * Creates key.
     *
     * @param nodeID node ID.
     * @param certType certificate type.
     * @param subject subject.
     * @param password password.
     * @param[out] csr certificate signing request.
     * @returns Error.
     */
    Error CreateKey(
        const String& nodeID, const String& certType, const String& subject, const String& password, String& csr);

    /**
     * Applies certificate.
     *
     * @param nodeID node ID.
     * @param certType certificate type.
     * @param pemCert certificate in PEM.
     * @param certInfo certificate info.
     * @returns Error.
     */
    Error ApplyCert(const String& nodeID, const String& certType, const String& pemCert, CertInfo& certInfo);

    /**
     * Reconnects to the server.
     *
     * @returns Error.
     */
    Error Reconnect();

private:
    static constexpr auto cServiceTimeout = std::chrono::seconds(10);

    std::string                                                 mIAMProtectedServerURL;
    std::string                                                 mCertStorage;
    bool                                                        mInsecureConnection {false};
    std::shared_ptr<grpc::ChannelCredentials>                   mCredentials;
    TLSCredentialsItf*                                          mTLSCredentials {};
    std::unique_ptr<iamanager::v6::IAMCertificateService::Stub> mStub;
    std::mutex                                                  mMutex;
};

} // namespace aos::common::iamclient

#endif
