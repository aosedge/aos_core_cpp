/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CERTPROVIDER_HPP_
#define CERTPROVIDER_HPP_

#include <gmock/gmock.h>
#include <grpc++/security/credentials.h>

#include "common/iamclient/publicservicehandler.hpp"

using namespace aos::common::iamclient;

class MockCertProvider : public TLSCredentialsItf {
public:
    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetMTLSClientCredentials,
        (const aos::String& certStorage), (override));

    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetTLSClientCredentials, (), (override));

    MOCK_METHOD(aos::Error, GetCert,
        (const aos::String& certType, const aos::Array<uint8_t>& issuer, const aos::Array<uint8_t>& serial,
            aos::iam::certhandler::CertInfo& certInfo),
        (const, override));

    MOCK_METHOD(aos::Error, SubscribeCertChanged,
        (const aos::String& certType, aos::iam::certhandler::CertReceiverItf& subscriber), (override));

    MOCK_METHOD(aos::Error, UnsubscribeCertChanged, (aos::iam::certhandler::CertReceiverItf & subscriber), (override));
};

#endif /* CERTPROVIDER_HPP_ */
