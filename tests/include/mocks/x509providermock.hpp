/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef X509_PROVIDER_MOCK_HPP_
#define X509_PROVIDER_MOCK_HPP_

#include <aos/common/crypto.hpp>
#include <gmock/gmock.h>

/**
 * Provides interface to mock manage certificate requests.
 */
class ProviderItfMock : public aos::crypto::x509::ProviderItf {
public:
    MOCK_METHOD(aos::Error, CreateCertificate,
        (const aos::crypto::x509::Certificate&, const aos::crypto::x509::Certificate&,
            const aos::crypto::PrivateKeyItf&, aos::String&),
        (override));
    MOCK_METHOD(aos::Error, CreateClientCert,
        (const aos::String&, const aos::String&, const aos::String&, const aos::Array<uint8_t>&, aos::String&),
        (override));
    MOCK_METHOD(
        aos::Error, PEMToX509Certs, (const aos::String&, aos::Array<aos::crypto::x509::Certificate>&), (override));
    MOCK_METHOD(aos::Error, X509CertToPEM, (const aos::crypto::x509::Certificate&, aos::String&), (override));
    MOCK_METHOD(aos::RetWithError<aos::SharedPtr<aos::crypto::PrivateKeyItf>>, PEMToX509PrivKey, (const aos::String&),
        (override));
    MOCK_METHOD(aos::Error, DERToX509Cert, (const aos::Array<uint8_t>&, aos::crypto::x509::Certificate&), (override));
    MOCK_METHOD(aos::Error, CreateCSR, (const aos::crypto::x509::CSR&, const aos::crypto::PrivateKeyItf&, aos::String&),
        (override));
    MOCK_METHOD(aos::Error, ASN1EncodeDN, (const aos::String&, aos::Array<uint8_t>&), (override));
    MOCK_METHOD(aos::Error, ASN1DecodeDN, (const aos::Array<uint8_t>&, aos::String&), (override));
    MOCK_METHOD(aos::Error, ASN1EncodeObjectIds,
        (const aos::Array<aos::crypto::asn1::ObjectIdentifier>&, aos::Array<uint8_t>&), (override));
    MOCK_METHOD(aos::Error, ASN1EncodeBigInt, (const aos::Array<uint8_t>&, aos::Array<uint8_t>&), (override));
    MOCK_METHOD(
        aos::Error, ASN1EncodeDERSequence, (const aos::Array<aos::Array<uint8_t>>&, aos::Array<uint8_t>&), (override));
    MOCK_METHOD(aos::Error, ASN1DecodeOctetString, (const aos::Array<uint8_t>&, aos::Array<uint8_t>&), (override));
    MOCK_METHOD(aos::Error, ASN1DecodeOID, (const aos::Array<uint8_t>&, aos::Array<uint8_t>&), (override));
};

#endif
