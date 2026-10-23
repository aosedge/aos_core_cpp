/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_CERTPROVIDERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_CERTPROVIDERSTUB_HPP_

#include <core/common/iamclient/itf/certprovider.hpp>

namespace aos::iamclient {

/**
 * Certificate provider stub.
 */
class CertProviderStub : public CertProviderItf {
public:
    Error GetCert(const String& storageID, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
        CertInfo& resCert) const override
    {
        (void)storageID;
        (void)issuer;
        (void)serial;
        (void)resCert;

        return ErrorEnum::eNone;
    }

    Error GetRootCerts(Array<CertInfo>& resCerts) const override
    {
        (void)resCerts;

        return ErrorEnum::eNone;
    }

    Error SubscribeListener(const String& storageID, CertListenerItf& certListener) override
    {
        (void)storageID;

        mListener = &certListener;

        return ErrorEnum::eNone;
    }

    Error SubscribeRootCertsListener(CertListenerItf& certListener) override
    {
        mListener = &certListener;

        return ErrorEnum::eNone;
    }

    Error UnsubscribeListener(CertListenerItf& certListener) override
    {
        if (mListener == &certListener) {
            mListener = nullptr;

            return ErrorEnum::eNone;
        }

        return ErrorEnum::eNotFound;
    }

    CertListenerItf* GetListener() const { return mListener; }

private:
    CertListenerItf* mListener {};
};

} // namespace aos::iamclient

#endif
