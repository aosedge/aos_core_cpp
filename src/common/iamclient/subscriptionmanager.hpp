/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_SUBSCRIPTIONMANAGER_HPP_
#define AOS_COMMON_IAMCLIENT_SUBSCRIPTIONMANAGER_HPP_

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include <grpcpp/grpcpp.h>

#include <core/common/tools/error.hpp>
#include <core/iam/certhandler/certprovider.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

namespace aos::common::iamclient {

/**
 * Manages subscription lifecycle for a single certificate type.
 */
class SubscriptionManager {
public:
    /**
     * Constructor.
     *
     * @param certType Certificate type to subscribe to.
     * @param stub IAM public service stub.
     */
    SubscriptionManager(std::string certType, iamanager::v5::IAMPublicService::Stub* stub);

    /**
     * Destructor - ensures clean shutdown.
     */
    ~SubscriptionManager();

    // Non-copyable, non-movable
    SubscriptionManager(const SubscriptionManager&)            = delete;
    SubscriptionManager& operator=(const SubscriptionManager&) = delete;
    SubscriptionManager(SubscriptionManager&&)                 = delete;
    SubscriptionManager& operator=(SubscriptionManager&&)      = delete;

    /**
     * Adds a subscriber.
     *
     * @param certReceiver Certificate receiver to add.
     * @return Error.
     */
    Error AddSubscriber(iam::certhandler::CertReceiverItf& certReceiver);

    /**
     * Removes a subscriber.
     *
     * @param certReceiver Certificate receiver to remove.
     * @return true if this was the last subscriber and task was stopped.
     */
    bool RemoveSubscriber(iam::certhandler::CertReceiverItf& certReceiver);

    /**
     * Explicitly closes the subscription manager and stops the task.
     * Safe to call multiple times. Should be called before the stub becomes invalid.
     */
    void Close();

private:
    /**
     * Starts the subscription task.
     */
    void Start();

    /**
     * Stops the subscription task.
     */
    void Stop();

    /**
     * Background task that maintains the gRPC subscription.
     */
    void RunTask();

    static constexpr auto cReconnectInterval = std::chrono::seconds(3);

    std::string                                            mCertType;
    iamanager::v5::IAMPublicService::Stub*                 mStub;
    std::mutex                                             mMutex;
    std::condition_variable                                mCV;
    std::unordered_set<iam::certhandler::CertReceiverItf*> mSubscribers;
    std::unique_ptr<grpc::ClientContext>                   mCtx;
    std::future<void>                                      mFuture;
    bool                                                   mClose {false};
    bool                                                   mRunning {false};
};

} // namespace aos::common::iamclient

#endif
