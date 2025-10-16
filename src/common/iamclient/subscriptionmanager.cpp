/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "subscriptionmanager.hpp"

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

SubscriptionManager::SubscriptionManager(std::string certType, iamanager::v5::IAMPublicService::Stub* stub)
    : mCertType(std::move(certType))
    , mStub(stub)
{
}

SubscriptionManager::~SubscriptionManager()
{
    Close();
}

Error SubscriptionManager::AddSubscriber(iam::certhandler::CertReceiverItf& certReceiver)
{
    std::lock_guard lock {mMutex};

    if (!mSubscribers.insert(&certReceiver).second) {
        return Error(ErrorEnum::eAlreadyExist, "subscriber already exists for this cert type");
    }

    // Start task on first subscriber
    if (mSubscribers.size() == 1 && !mRunning) {
        Start();
    }

    return ErrorEnum::eNone;
}

bool SubscriptionManager::RemoveSubscriber(iam::certhandler::CertReceiverItf& certReceiver)
{
    std::lock_guard lock {mMutex};

    mSubscribers.erase(&certReceiver);

    // Stop task when last subscriber is removed
    if (mSubscribers.empty() && mRunning) {
        Stop();
        return true;
    }

    return false;
}

void SubscriptionManager::Close()
{
    Stop();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void SubscriptionManager::Start()
{
    LOG_INF() << "Starting subscription task: certType=" << mCertType.c_str();

    mClose   = false;
    mRunning = true;
    mFuture  = std::async(std::launch::async, &SubscriptionManager::RunTask, this);
}

void SubscriptionManager::Stop()
{
    {
        std::lock_guard lock {mMutex};

        if (!mRunning) {
            return;
        }

        LOG_INF() << "Stopping subscription task: certType=" << mCertType.c_str();

        mClose = true;

        if (mCtx) {
            mCtx->TryCancel();
        }
    }

    mCV.notify_all();

    if (mFuture.valid()) {
        mFuture.wait();
    }

    {
        std::lock_guard lock {mMutex};
        mRunning = false;
    }
}

void SubscriptionManager::RunTask()
{
    LOG_DBG() << "Subscription task started: certType=" << mCertType.c_str();

    while (true) {
        try {
            {
                std::lock_guard lock {mMutex};

                if (mClose) {
                    break;
                }
            }

            auto                                       ctx = std::make_unique<grpc::ClientContext>();
            iamanager::v5::SubscribeCertChangedRequest request;

            request.set_type(mCertType);

            std::unique_ptr<grpc::ClientReader<iamanager::v5::CertInfo>> reader(
                mStub->SubscribeCertChanged(ctx.get(), request));

            {
                std::lock_guard lock {mMutex};

                mCtx = std::move(ctx);
            }

            iamanager::v5::CertInfo certInfo;

            while (reader->Read(&certInfo)) {
                LOG_INF() << "Certificate changed: certURL=" << certInfo.cert_url().c_str()
                          << ", keyURL=" << certInfo.key_url().c_str();

                {
                    std::lock_guard lock {mMutex};

                    for (auto subscriber : mSubscribers) {
                        iam::certhandler::CertInfo iamCertInfo;

                        iamCertInfo.mCertURL = certInfo.cert_url().c_str();
                        iamCertInfo.mKeyURL  = certInfo.key_url().c_str();

                        subscriber->OnCertChanged(iamCertInfo);
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERR() << "Subscription loop failed: err=" << e.what();
        }

        {
            std::unique_lock lock {mMutex};

            mCV.wait_for(lock, cReconnectInterval, [this]() { return mClose; });

            if (mClose) {
                break;
            }
        }
    }

    LOG_DBG() << "Subscription task stopped: certType=" << mCertType.c_str();
}

} // namespace aos::common::iamclient
