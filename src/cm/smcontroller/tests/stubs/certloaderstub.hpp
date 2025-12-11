/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_CERTLOADERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_CERTLOADERSTUB_HPP_

#include <core/common/crypto/itf/certloader.hpp>

namespace aos::crypto {

/**
 * Certificate loader stub.
 */
class CertLoaderStub : public CertLoaderItf {
public:
    RetWithError<SharedPtr<x509::CertificateChain>> LoadCertsChainByURL(const String& url) override
    {
        (void)url;
        return {
            SharedPtr<x509::CertificateChain>(),
            ErrorEnum::eNone,
        };
    }

    RetWithError<SharedPtr<PrivateKeyItf>> LoadPrivKeyByURL(const String& url) override
    {
        (void)url;
        return {SharedPtr<PrivateKeyItf>()};
    }

    x509::CertificateChain mCertChain;
};

} // namespace aos::crypto

#endif
