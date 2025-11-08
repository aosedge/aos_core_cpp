/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_X509PROVIDERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_X509PROVIDERSTUB_HPP_

#include <core/common/crypto/itf/crypto.hpp>

namespace aos::crypto::x509 {

/**
 * X509 provider stub.
 */
class ProviderStub : public ProviderItf {
public:
    Error CreateCertificate(const Certificate& templ, const Certificate& parent, const crypto::PrivateKeyItf& privKey,
        String& pemCert) override
    {
        (void)templ;
        (void)parent;
        (void)privKey;
        (void)pemCert;

        return ErrorEnum::eNone;
    }

    Error CreateClientCert(const String& csr, const String& caKey, const String& caCert, const Array<uint8_t>& serial,
        String& clientCert) override
    {
        (void)csr;
        (void)caKey;
        (void)caCert;
        (void)serial;
        (void)clientCert;

        return ErrorEnum::eNone;
    }

    Error PEMToX509Certs(const String& pemBlob, Array<Certificate>& resultCerts) override
    {
        (void)pemBlob;
        (void)resultCerts;

        return ErrorEnum::eNone;
    }

    Error X509CertToPEM(const Certificate& certificate, String& dst) override
    {
        (void)certificate;
        (void)dst;

        return ErrorEnum::eNone;
    }

    RetWithError<SharedPtr<crypto::PrivateKeyItf>> PEMToX509PrivKey(const String& pemBlob) override
    {
        (void)pemBlob;

        return {SharedPtr<crypto::PrivateKeyItf>()};
    }

    Error DERToX509Cert(const Array<uint8_t>& derBlob, Certificate& resultCert) override
    {
        (void)derBlob;
        (void)resultCert;

        return ErrorEnum::eNone;
    }

    Error CreateCSR(const CSR& templ, const PrivateKeyItf& privKey, String& pemCSR) override
    {
        (void)templ;
        (void)privKey;
        (void)pemCSR;

        return ErrorEnum::eNone;
    }

    Error ASN1EncodeDN(const String& commonName, Array<uint8_t>& result) override
    {
        (void)commonName;
        (void)result;

        return ErrorEnum::eNone;
    }

    Error ASN1DecodeDN(const Array<uint8_t>& dn, String& result) override
    {
        (void)dn;
        (void)result;

        return ErrorEnum::eNone;
    }

    Error ASN1EncodeObjectIds(const Array<asn1::ObjectIdentifier>& src, Array<uint8_t>& asn1Value) override
    {
        (void)src;
        (void)asn1Value;

        return ErrorEnum::eNone;
    }

    Error ASN1EncodeBigInt(const Array<uint8_t>& number, Array<uint8_t>& asn1Value) override
    {
        (void)number;
        (void)asn1Value;

        return ErrorEnum::eNone;
    }

    Error ASN1EncodeDERSequence(const Array<Array<uint8_t>>& items, Array<uint8_t>& asn1Value) override
    {
        (void)items;
        (void)asn1Value;

        return ErrorEnum::eNone;
    }

    Error ASN1DecodeOctetString(const Array<uint8_t>& src, Array<uint8_t>& dst) override
    {
        (void)src;
        (void)dst;

        return ErrorEnum::eNone;
    }

    Error ASN1DecodeOID(const Array<uint8_t>& inOID, Array<uint8_t>& dst) override
    {
        (void)inOID;
        (void)dst;

        return ErrorEnum::eNone;
    }

    Error Verify(const Variant<ECDSAPublicKey, RSAPublicKey>& pubKey, Hash hashFunc, Padding padding,
        const Array<uint8_t>& digest, const Array<uint8_t>& signature) override
    {
        (void)pubKey;
        (void)hashFunc;
        (void)padding;
        (void)digest;
        (void)signature;

        return ErrorEnum::eNone;
    }

    Error Verify(const Array<Certificate>& rootCerts, const Array<Certificate>& intermCerts,
        const VerifyOptions& options, const Certificate& cert) override
    {
        (void)rootCerts;
        (void)intermCerts;
        (void)options;
        (void)cert;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::crypto::x509

#endif
