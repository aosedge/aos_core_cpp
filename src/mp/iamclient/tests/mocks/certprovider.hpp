/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_IAMCLIENT_CERTPROVIDER_HPP_
#define AOS_MP_IAMCLIENT_CERTPROVIDER_HPP_

#include <gmock/gmock.h>
#include <grpc++/security/credentials.h>

#include "common/iamclient/publicservicehandler.hpp"

namespace aos::common::iamclient {

class MockCertProvider : public TLSCredentialsItf {
public:
    MOCK_METHOD(RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetMTLSClientCredentials,
        (const String& certStorage), (override));

    MOCK_METHOD(RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetTLSClientCredentials, (), (override));

    MOCK_METHOD(Error, GetCert,
        (const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
            iam::certhandler::CertInfo& certInfo),
        (const, override));

    MOCK_METHOD(Error, SubscribeCertChanged, (const String& certType, iam::certhandler::CertReceiverItf& subscriber),
        (override));

    MOCK_METHOD(Error, UnsubscribeCertChanged, (iam::certhandler::CertReceiverItf & subscriber), (override));
};

} // namespace aos::common::iamclient

#endif
