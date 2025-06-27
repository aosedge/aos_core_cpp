/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_IAMCLIENT_PUBLICNODECLIENT_HPP_
#define AOS_MP_IAMCLIENT_PUBLICNODECLIENT_HPP_

#include <atomic>
#include <condition_variable>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Poco/Runnable.h>
#include <Poco/ThreadPool.h>
#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>

#include <aos/common/tools/error.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include <common/iamclient/publicservicehandler.hpp>
#include <common/utils/channel.hpp>
#include <common/utils/time.hpp>
#include <mp/communication/types.hpp>
#include <mp/config/config.hpp>

namespace aos::mp::iamclient {

/**
 * Public node client interface.
 */
class PublicNodeClient : public communication::HandlerItf, public iam::certhandler::CertReceiverItf {
public:
    /**
     * Constructor.
     */
    PublicNodeClient() = default;

    /**
     * Initializes the client.
     *
     * @param cfg Configuration.
     * @param certProvider Certificate provider.
     * @param publicServer Public server.
     * @return Error error code.
     */
    Error Init(
        const config::IAMConfig& cfg, common::iamclient::TLSCredentialsItf& certProvider, bool publicServer = true);

    /**
     * Notifies that connection is established.
     */
    void OnConnected() override;

    /**
     * Notifies that connection is lost.
     */
    void OnDisconnected() override;

    /**
     * Sends messages.
     *
     * @param messages Messages.
     * @return Error error code.
     */
    Error SendMessages(std::vector<uint8_t> messages) override;

    /**
     * Receives messages.
     *
     * @return Messages.
     */
    RetWithError<std::vector<uint8_t>> ReceiveMessages() override;

    /**
     * Subscribes to certificate changes.
     *
     * @param certType Certificate type.
     */
    void OnCertChanged(const iam::certhandler::CertInfo& info) override;

private:
    using StreamPtr = std::unique_ptr<
        grpc::ClientReaderWriterInterface<iamanager::v5::IAMOutgoingMessages, iamanager::v5::IAMIncomingMessages>>;
    using PublicNodeService        = iamanager::v5::IAMPublicNodesService;
    using PublicNodeServiceStubPtr = std::unique_ptr<PublicNodeService::StubInterface>;
    using HandlerFunc              = std::function<Error(const iamanager::v5::IAMIncomingMessages&)>;

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

    static constexpr auto cReconnectInterval = std::chrono::seconds(3);

    Error CreateCredentials();
    void  ConnectionLoop(const std::string& url) noexcept;
    Error HandleIncomingMessages();
    Error RegisterNode(const std::string& url);
    void  InitializeHandlers();
    void  ProcessOutgoingIAMMessages();
    void  Close();
    void  CacheMessage(const iamanager::v5::IAMOutgoingMessages& message);
    Error SendCachedMessages();

    std::vector<std::shared_ptr<grpc::ChannelCredentials>> mCredentialList;
    std::string                                            mCertStorage;
    common::iamclient::TLSCredentialsItf*                  mCertProvider {};

    std::unique_ptr<grpc::ClientContext> mRegisterNodeCtx;
    StreamPtr                            mStream;
    PublicNodeServiceStubPtr             mPublicNodeServiceStub;
    Poco::ThreadPool                     mThreadPool;

    std::thread             mConnectionThread;
    std::thread             mHandlerOutgoingMsgsThread;
    std::condition_variable mCV;
    std::atomic<bool>       mShutdown {};
    bool                    mConnected {};
    bool                    mNotifyConnected {};
    std::mutex              mMutex;
    std::string             mUrl;
    bool                    mPublicServer {};

    common::utils::Channel<std::vector<uint8_t>> mOutgoingMsgChannel;
    common::utils::Channel<std::vector<uint8_t>> mIncomingMsgChannel;

    std::queue<iamanager::v5::IAMOutgoingMessages> mMessageCache;
};

} // namespace aos::mp::iamclient

#endif
