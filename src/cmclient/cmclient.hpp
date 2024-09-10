/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CMCLIENT_HPP_
#define CMCLIENT_HPP_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>

#include <grpcpp/security/credentials.h>
#include <servicemanager/v4/servicemanager.grpc.pb.h>

#include <aos/common/cryptoutils.hpp>
#include <aos/common/tools/error.hpp>
#include <utils/channel.hpp>

#include "communication/types.hpp"
#include "config/config.hpp"
#include "iamclient/types.hpp"

using SMService        = servicemanager::v4::SMService;
using SMServiceStubPtr = std::unique_ptr<SMService::StubInterface>;

namespace aos::mp::cmclient {

/**
 * CMClient class.
 */
class CMClient : public communication::HandlerItf {
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
    Error Init(const config::Config& config, iamclient::CertProviderItf& certProvider,
        cryptoutils::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
        bool insecureConnection = false);

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

private:
    constexpr static auto cReconnectTimeout = std::chrono::seconds(3);

    using StreamPtr = std::unique_ptr<grpc::ClientReaderWriterInterface<::servicemanager::v4::SMOutgoingMessages,
        servicemanager::v4::SMIncomingMessages>>;

    void                                                    RunCM(std::string url);
    SMServiceStubPtr                                        CreateSMStub(const std::string& url);
    void                                                    RegisterSM(const std::string& url);
    void                                                    ProcessIncomingSMMessage();
    void                                                    ProcessOutgoingSMMessages();
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> CreateCredentials(
        const std::string& certStorage, bool insecureConnection);
    void Close();
    void CacheMessage(const servicemanager::v4::SMOutgoingMessages& message);
    void SendCachedMessages();

    std::thread mCMThread;
    std::thread mHandlerOutgoingMsgsThread;

    std::atomic<bool> mShutdown {false};
    bool              mCMConnected {false};

    std::mutex              mMutex;
    std::condition_variable mCV;

    std::shared_ptr<grpc::ChannelCredentials> mCredentials;
    SMServiceStubPtr                          mSMStub;
    StreamPtr                                 mStream;
    std::unique_ptr<grpc::ClientContext>      mCtx;
    std::string                               mUrl;

    iamclient::CertProviderItf*                        mCertProvider {};
    cryptoutils::CertLoaderItf*                        mCertLoader {};
    crypto::x509::ProviderItf*                         mCryptoProvider {};
    common::utils::Channel<std::vector<uint8_t>>       mOutgoingMsgChannel;
    common::utils::Channel<std::vector<uint8_t>>       mIncomingMsgChannel;
    bool                                               mNotifyConnected {};
    std::queue<servicemanager::v4::SMOutgoingMessages> mMessageCache;
};

} // namespace aos::mp::cmclient

#endif
