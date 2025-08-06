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
#include <thread>
#include <vector>

#include <core/iam/certhandler/certprovider.hpp>

namespace aos::iam::certhandler {

class CertProviderStub : public CertProviderItf {
public:
    CertProviderStub(iam::certhandler::CertHandler& certHandler)
        : mCertHandler(certHandler)
    {
    }

    Error GetCert(const String& certType, [[maybe_unused]] const Array<uint8_t>& issuer,
        [[maybe_unused]] const Array<uint8_t>& serial, iam::certhandler::CertInfo& resCert) const override
    {
        mCertCalled = true;
        mCondVar.notify_all();

        mCertHandler.GetCertificate(certType, {}, {}, resCert);

        return ErrorEnum::eNone;
    }

    Error SubscribeCertChanged([[maybe_unused]] const String& certType,
        [[maybe_unused]] iam::certhandler::CertReceiverItf&   subscriber) override
    {
        return ErrorEnum::eNone;
    }

    Error UnsubscribeCertChanged([[maybe_unused]] iam::certhandler::CertReceiverItf& subscriber) override
    {
        return ErrorEnum::eNone;
    }

    bool IsCertCalled()
    {
        std::unique_lock lock {mMutex};

        mCondVar.wait_for(lock, cWaitTimeout, [this] { return mCertCalled.load(); });

        return mCertCalled;
    }

    void ResetCertCalled() { mCertCalled = false; }

private:
    iam::certhandler::CertHandler&  mCertHandler;
    mutable std::atomic_bool        mCertCalled {};
    std::mutex                      mMutex;
    mutable std::condition_variable mCondVar;
    constexpr static auto           cWaitTimeout = std::chrono::seconds(3);
};

} // namespace aos::iam::certhandler

#endif
