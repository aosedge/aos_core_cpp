/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_CMCLIENT_CMCLIENT_HPP_
#define AOS_MP_CMCLIENT_CMCLIENT_HPP_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>

#include <Poco/Runnable.h>
#include <Poco/ThreadPool.h>

#include <grpcpp/security/credentials.h>
#include <servicemanager/v4/servicemanager.grpc.pb.h>

#include <aos/common/crypto/utils.hpp>
#include <aos/common/tools/error.hpp>

#include "common/iamclient/publicservicehandler.hpp"
#include "common/utils/channel.hpp"
#include "mp/communication/types.hpp"
#include "mp/config/config.hpp"

using SMService        = servicemanager::v4::SMService;
using SMServiceStubPtr = std::unique_ptr<SMService::StubInterface>;

namespace aos::mp::cmclient {

/**
 * CMClient class.
 */
class CMClient : public communication::HandlerItf, public iam::certhandler::CertReceiverItf {
public:
    /**
     *  Initializes CMClient.
     *
     * @param config configuration.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param insecureConnection insecure connection.
     * @return Error.
     */
    Error Init(const config::Config& config, common::iamclient::TLSCredentialsItf& certProvider,
        crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider, bool insecureConnection = false);

    /**
     * Notifies that connection is established.
     *
     */
    void OnConnected() override;

    /**
     * Notifies that connection is lost.
     *
     */
    void OnDisconnected() override;

    /**
     * Sends messages.
     *
     * @param messages messages.
     * @return Error.
     */
    Error SendMessages(std::vector<uint8_t> messages) override;

    /**
     * Receives messages.
     *
     * @return RetWithError<std::vector<uint8_t>>.
     */
    RetWithError<std::vector<uint8_t>> ReceiveMessages() override;

    /**
     * Subscribes to certificate changes.
     *
     * @param certType Certificate type.
     */
    void OnCertChanged(const iam::certhandler::CertInfo& info) override;

private:
    constexpr static auto cReconnectTimeout = std::chrono::seconds(3);

    using StreamPtr = std::unique_ptr<grpc::ClientReaderWriterInterface<::servicemanager::v4::SMOutgoingMessages,
        servicemanager::v4::SMIncomingMessages>>;

    template <typename F>
    class RunnableWrapper : public Poco::Runnable {
        F mFunc;

    public:
        explicit RunnableWrapper(F&& func)
            : mFunc(std::move(func))
        {
        }

        void run() override { mFunc(); }
    };

    template <typename F>
    static auto makeRunnable(F&& f)
    {
        return new RunnableWrapper<F>(std::forward<F>(f));
    }

    void                                                    RunCM(const std::string& url);
    SMServiceStubPtr                                        CreateSMStub(const std::string& url);
    void                                                    RegisterSM(const std::string& url);
    void                                                    ProcessIncomingSMMessage();
    void                                                    ProcessOutgoingSMMessages();
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> CreateCredentials();
    void                                                    Close();
    void CacheMessage(const servicemanager::v4::SMOutgoingMessages& message);
    void SendCachedMessages();

    std::thread      mCMThread;
    std::thread      mHandlerOutgoingMsgsThread;
    Poco::ThreadPool mThreadPool;

    std::atomic<bool> mShutdown {false};
    bool              mCMConnected {false};
    bool              mInsecureConnection {};
    std::string       mCertStorage;

    std::mutex              mMutex;
    std::condition_variable mCV;

    std::shared_ptr<grpc::ChannelCredentials> mCredentials;
    SMServiceStubPtr                          mSMStub;
    StreamPtr                                 mStream;
    std::unique_ptr<grpc::ClientContext>      mCtx;
    std::string                               mUrl;

    common::iamclient::TLSCredentialsItf*              mCertProvider {};
    crypto::CertLoaderItf*                             mCertLoader {};
    crypto::x509::ProviderItf*                         mCryptoProvider {};
    common::utils::Channel<std::vector<uint8_t>>       mOutgoingMsgChannel;
    common::utils::Channel<std::vector<uint8_t>>       mIncomingMsgChannel;
    bool                                               mNotifyConnected {};
    std::queue<servicemanager::v4::SMOutgoingMessages> mMessageCache;
};

} // namespace aos::mp::cmclient

#endif
