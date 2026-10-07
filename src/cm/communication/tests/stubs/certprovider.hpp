/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_STUBS_CERTPROVIDER_HPP_
#define AOS_CM_COMMUNICATION_TESTS_STUBS_CERTPROVIDER_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <core/common/iamclient/itf/certprovider.hpp>

namespace aos::iamclient {

class CertProviderStub : public CertProviderItf {
public:
    CertProviderStub(iam::certhandler::CertHandler& certHandler)
        : mCertHandler(certHandler)
    {
    }

    Error GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
        CertInfo& resCert) const override
    {
        (void)issuer;
        (void)serial;

        mCertCalled = true;
        mCondVar.notify_all();

        mCertHandler.GetCert(certType, {}, {}, resCert);

        return ErrorEnum::eNone;
    }

    Error GetAllCerts(const String& certType, Array<CertInfo>& resCerts) const override
    {
        if (certType == cRootCertType) {
            for (const auto& certURL : mRootCertURLs) {
                CertInfo certInfo;

                certInfo.mCertURL = certURL.c_str();

                if (auto err = resCerts.PushBack(certInfo); !err.IsNone()) {
                    return err;
                }
            }

            return ErrorEnum::eNone;
        }

        return mCertHandler.GetAllCerts(certType, resCerts);
    }

    Error SubscribeListener(const String& certType, CertListenerItf& certListener) override
    {
        (void)certType;
        (void)certListener;

        return ErrorEnum::eNone;
    }

    Error UnsubscribeListener(CertListenerItf& certListener) override
    {
        (void)certListener;

        return ErrorEnum::eNone;
    }

    bool IsCertCalled()
    {
        std::unique_lock lock {mMutex};

        mCondVar.wait_for(lock, cWaitTimeout, [this] { return mCertCalled.load(); });

        return mCertCalled;
    }

    void ResetCertCalled() { mCertCalled = false; }

    void SetRootCerts(const std::vector<std::string>& certURLs) { mRootCertURLs = certURLs; }

private:
    static constexpr auto cRootCertType = "rootcerts";

    iam::certhandler::CertHandler&  mCertHandler;
    mutable std::atomic_bool        mCertCalled {};
    std::mutex                      mMutex;
    mutable std::condition_variable mCondVar;
    std::vector<std::string>        mRootCertURLs;
    constexpr static auto           cWaitTimeout = std::chrono::seconds(3);
};

} // namespace aos::iamclient

#endif
