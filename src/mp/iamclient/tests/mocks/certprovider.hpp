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

#include <common/iamclient/publicservicehandler.hpp>

namespace aos::common::iamclient {

class MockCertProvider : public TLSCredentialsItf {
public:
    MOCK_METHOD(
        RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetMTLSClientCredentials, (const String&), (override));
    MOCK_METHOD(RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetTLSClientCredentials, (), (override));
    MOCK_METHOD(
        Error, GetCert, (const String&, const Array<uint8_t>&, const Array<uint8_t>&, CertInfo&), (const, override));
    MOCK_METHOD(Error, SubscribeListener, (const String&, aos::iamclient::CertListenerItf&), (override));
    MOCK_METHOD(Error, UnsubscribeListener, (aos::iamclient::CertListenerItf&), (override));
};

} // namespace aos::common::iamclient

#endif
