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

#include <iamclient/publicservicehandler.hpp>

using namespace aos::common::iamclient;

class MockCertProvider : public CertProviderItf {
public:
    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetMTLSConfig,
        (const std::string& certStorage), (override));

    MOCK_METHOD(std::shared_ptr<grpc::ChannelCredentials>, GetTLSCredentials, (), (override));

    MOCK_METHOD(aos::Error, GetCertificate, (const std::string& certType, aos::iam::certhandler::CertInfo& certInfo),
        (override));

    MOCK_METHOD(aos::Error, SubscribeCertChanged,
        (const std::string& certType, aos::iam::certhandler::CertReceiverItf& subscriber), (override));

    MOCK_METHOD(void, UnsubscribeCertChanged,
        (const std::string& certType, aos::iam::certhandler::CertReceiverItf& subscriber), (override));
};

#endif /* CERTPROVIDER_HPP_ */
