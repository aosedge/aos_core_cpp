/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CERT_HANDLER_MOCK_HPP_
#define CERT_HANDLER_MOCK_HPP_

#include <aos/iam/certhandler.hpp>
#include <gmock/gmock.h>

/**
 * Certificate handler mock.
 */
class CertHandlerItfMock : public aos::iam::certhandler::CertHandlerItf {
public:
    MOCK_METHOD(aos::Error, GetCertTypes, (aos::Array<aos::StaticString<aos::iam::certhandler::cCertTypeLen>>&),
        (const override));
    MOCK_METHOD(aos::Error, SetOwner, (const aos::String&, const aos::String&), (override));
    MOCK_METHOD(aos::Error, Clear, (const aos::String&), (override));
    MOCK_METHOD(
        aos::Error, CreateKey, (const aos::String&, const aos::String&, const aos::String&, aos::String&), (override));
    MOCK_METHOD(aos::Error, ApplyCertificate,
        (const aos::String&, const aos::String&, aos::iam::certhandler::CertInfo&), (override));
    MOCK_METHOD(aos::Error, GetCertificate,
        (const aos::String&, const aos::Array<uint8_t>&, const aos::Array<uint8_t>&, aos::iam::certhandler::CertInfo&),
        (override));
    MOCK_METHOD(aos::Error, SubscribeCertChanged,
        (const aos::String& certType, aos::iam::certhandler::CertReceiverItf& certReceiver), (override));
    MOCK_METHOD(
        aos::Error, UnsubscribeCertChanged, (aos::iam::certhandler::CertReceiverItf & certReceiver), (override));
    MOCK_METHOD(aos::Error, CreateSelfSignedCert, (const aos::String&, const aos::String&), (override));
    MOCK_METHOD(aos::RetWithError<aos::iam::certhandler::ModuleConfig>, GetModuleConfig, (const aos::String&),
        (const, override));
};

#endif
