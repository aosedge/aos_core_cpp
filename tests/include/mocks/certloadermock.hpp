/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CERT_LOADER_MOCK_HPP_
#define CERT_LOADER_MOCK_HPP_

#include <aos/iam/certhandler.hpp>
#include <gmock/gmock.h>

/**
 * Mocks load certificates and keys interface.
 */

class CertLoaderItfMock : public aos::cryptoutils::CertLoaderItf {
public:
    MOCK_METHOD(aos::Error, Init, (aos::crypto::x509::ProviderItf&, aos::pkcs11::PKCS11Manager&), (override));
    MOCK_METHOD(aos::RetWithError<aos::SharedPtr<aos::crypto::x509::CertificateChain>>, LoadCertsChainByURL,
        (const aos::String&), (override));
    MOCK_METHOD(aos::RetWithError<aos::SharedPtr<aos::crypto::PrivateKeyItf>>, LoadPrivKeyByURL, (const aos::String&),
        (override));
};

#endif
