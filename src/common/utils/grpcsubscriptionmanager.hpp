/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_GRPCSUBSCRIPTIONMANAGER_HPP_
#define AOS_COMMON_UTILS_GRPCSUBSCRIPTIONMANAGER_HPP_

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include <grpcpp/grpcpp.h>

#include <core/common/tools/logger.hpp>

#include <core/common/tools/error.hpp>

namespace aos::common::utils {

/**
 * Generic subscription manager template that handles gRPC stream subscriptions.
 *
 * @tparam TStub gRPC stub type (e.g., IAMPublicCertService::Stub).
 * @tparam TListener Listener interface type (e.g., CertListenerItf).
 * @tparam TProtoMsg Protobuf message type (e.g., iamanager::v6::CertInfo).
 * @tparam TAosType AOS native type (e.g., CertInfo).
 * @tparam TRequest Protobuf request type (e.g., SubscribeCertChangedRequest).
 */
template <typename TStub, typename TListener, typename TProtoMsg, typename TAosType, typename TRequest>
class GRPCSubscriptionManager {
public:
    // Type aliases for function pointers
    using ReaderPtr   = std::unique_ptr<grpc::ClientReader<TProtoMsg>>;
    using ReaderFunc  = ReaderPtr (TStub::*)(grpc::ClientContext*, const TRequest&);
    using ConvertFunc = std::function<Error(const TProtoMsg&, TAosType&)>;
    using NotifyFunc  = std::function<void(TListener&, const TAosType&)>;

    /**
     * Constructor.
     *
     * @param stub gRPC service stub.
     * @param request Subscription request.
     * @param readerFunc Pointer to stub's subscription method.
     * @param convertFunc Function to convert proto message to AOS type.
     * @param notifyFunc Pointer to listener's notification method.
     * @param logContext Context string for logging.
     */
    GRPCSubscriptionManager(TStub* stub, TRequest request, ReaderFunc readerFunc, ConvertFunc convertFunc,
        NotifyFunc notifyFunc, const std::string& logContext = "")
        : mStub(stub)
        , mRequest(std::move(request))
        , mReaderFunc(readerFunc)
        , mConvertFunc(std::move(convertFunc))
        , mNotifyFunc(notifyFunc)
        , mLogContext(std::move(logContext))
    {
    }

    /**
     * Destructor.
     */
    ~GRPCSubscriptionManager() { Close(); }

    // Non-copyable, non-movable
    GRPCSubscriptionManager(const GRPCSubscriptionManager&)            = delete;
    GRPCSubscriptionManager& operator=(const GRPCSubscriptionManager&) = delete;
    GRPCSubscriptionManager(GRPCSubscriptionManager&&)                 = delete;
    GRPCSubscriptionManager& operator=(GRPCSubscriptionManager&&)      = delete;

    /**
     * Adds subscriber.
     *
     * @param listener Listener to add.
     * @return Error.
     */
    Error Subscribe(TListener& listener)
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Add subscriber" << Log::Field("context", mLogContext.c_str());

        if (!mSubscribers.insert(&listener).second) {
            return Error(ErrorEnum::eAlreadyExist, "subscriber already exists");
        }

        if (mSubscribers.size() == 1) {
            Start();
        }

        return ErrorEnum::eNone;
    }

    /**
     * Removes subscriber.
     *
     * @param listener Listener to remove.
     * @return true if this was the last subscriber and task was stopped.
     */
    bool Unsubscribe(TListener& listener)
    {
        bool shouldStop {};

        {
            std::lock_guard lock {mMutex};

            LOG_DBG() << "Remove subscriber" << Log::Field("context", mLogContext.c_str());

            mSubscribers.erase(&listener);

            shouldStop = mSubscribers.empty() && mThread.joinable();
        }

        if (shouldStop) {
            Stop();
        }

        return shouldStop;
    }

    /**
     * Explicitly closes the subscription manager and stops the task.
     * Safe to call multiple times. Should be called before the stub becomes invalid.
     */
    void Close() { Stop(); }

    /**
     * Reconnects the subscription manager with a new stub.
     * Stops the current subscription, updates the stub, and restarts if there are subscribers.
     *
     * @param stub New gRPC service stub.
     */
    void Reconnect(TStub* stub)
    {
        LOG_DBG() << "Reconnect subscription manager" << Log::Field("context", mLogContext.c_str());

        Stop();

        {
            std::lock_guard lock {mMutex};

            mStub = stub;

            if (!mSubscribers.empty()) {
                Start();
            }
        }
    }

private:
    void Start()
    {
        LOG_DBG() << "Starting subscription task" << Log::Field("context", mLogContext.c_str());

        mClose  = false;
        mThread = std::thread(&GRPCSubscriptionManager::RunTask, this);
    }

    void Stop()
    {
        {
            std::lock_guard lock {mMutex};

            if (!mThread.joinable()) {
                return;
            }

            LOG_DBG() << "Stopping subscription task" << Log::Field("context", mLogContext.c_str());

            mClose = true;

            if (mCtx) {
                mCtx->TryCancel();
            }
        }

        mCV.notify_all();

        if (mThread.joinable()) {
            mThread.join();
        }
    }

    void RunTask()
    {
        LOG_DBG() << "Subscription task started" << Log::Field("context", mLogContext.c_str());

        while (true) {
            try {
                ReaderPtr reader;

                {
                    std::lock_guard lock {mMutex};

                    if (mClose) {
                        break;
                    }

                    mCtx   = std::make_unique<grpc::ClientContext>();
                    reader = (mStub->*mReaderFunc)(mCtx.get(), mRequest);
                }

                TProtoMsg protoMsg;

                while (reader->Read(&protoMsg)) {
                    std::lock_guard lock {mMutex};

                    LOG_DBG() << "Received message on subscription" << Log::Field("context", mLogContext.c_str());

                    auto aosType = std::make_unique<TAosType>();

                    if (auto err = mConvertFunc(protoMsg, *aosType); !err.IsNone()) {
                        LOG_ERR() << "Conversion failed" << Log::Field("context", mLogContext.c_str())
                                  << Log::Field(err);

                        continue;
                    }

                    for (auto subscriber : mSubscribers) {
                        mNotifyFunc(*subscriber, *aosType);
                    }
                }

                if (auto status = reader->Finish(); !status.ok()) {
                    LOG_WRN() << "Stream finished with error" << Log::Field("context", mLogContext.c_str())
                              << Log::Field("error", status.error_message().c_str());
                }
            } catch (const std::exception& e) {
                LOG_ERR() << "Subscription loop failed" << Log::Field("context", mLogContext.c_str())
                          << Log::Field(Error {ErrorEnum::eRuntime, e.what()});
            }

            {
                std::unique_lock lock {mMutex};

                mCV.wait_for(lock, cReconnectInterval, [this]() { return mClose; });

                if (mClose) {
                    break;
                }
            }
        }

        LOG_DBG() << "Subscription task stopped" << Log::Field("context", mLogContext.c_str());
    }

    static constexpr auto cReconnectInterval = std::chrono::seconds(3);

    TStub*                               mStub;
    TRequest                             mRequest;
    ReaderFunc                           mReaderFunc;
    ConvertFunc                          mConvertFunc;
    NotifyFunc                           mNotifyFunc;
    std::string                          mLogContext;
    std::mutex                           mMutex;
    std::condition_variable              mCV;
    std::unordered_set<TListener*>       mSubscribers;
    std::unique_ptr<grpc::ClientContext> mCtx;
    std::thread                          mThread;
    bool                                 mClose {false};
};

} // namespace aos::common::utils

#endif
