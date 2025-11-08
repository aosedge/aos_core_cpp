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
    Error GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
        CertInfo& resCert) const override
    {
        (void)certType;
        (void)issuer;
        (void)serial;
        (void)resCert;

        return ErrorEnum::eNone;
    }

    Error SubscribeListener(const String& certType, CertListenerItf& certListener) override
    {
        (void)certType;

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
