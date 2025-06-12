/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common/logger/logmodule.hpp"
#include "common/utils/grpchelper.hpp"

#include "cmclient.hpp"

namespace aos::mp::cmclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CMClient::Init(const config::Config& config, common::iamclient::TLSCredentialsItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider, bool insecureConnection)
{
    LOG_INF() << "Initializing CM client";

    mCertProvider       = &certProvider;
    mCertLoader         = &certLoader;
    mCryptoProvider     = &cryptoProvider;
    mUrl                = config.mCMConfig.mCMServerURL;
    mInsecureConnection = insecureConnection;
    mCertStorage        = config.mCertStorage;

    auto [credentials, err] = CreateCredentials();
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    return ErrorEnum::eNone;
}

Error CMClient::SendMessages(std::vector<uint8_t> messages)
{
    LOG_DBG() << "Sending messages";

    return mOutgoingMsgChannel.Send(std::move(messages));
}

RetWithError<std::vector<uint8_t>> CMClient::ReceiveMessages()
{
    LOG_DBG() << "Receiving messages";

    return mIncomingMsgChannel.Receive();
}

void CMClient::OnConnected()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Connected to CM";

    if (!mNotifyConnected) {
        mNotifyConnected = true;

        mCMThread                  = std::thread(&CMClient::RunCM, this, mUrl);
        mHandlerOutgoingMsgsThread = std::thread(&CMClient::ProcessOutgoingSMMessages, this);
    }
}

void CMClient::OnDisconnected()
{
    Close();
}

// cppcheck-suppress unusedFunction
void CMClient::OnCertChanged([[maybe_unused]] const iam::certhandler::CertInfo& info)
{
    LOG_DBG() << "Certificate changed";

    auto task = [this] {
        {
            std::lock_guard lock {mMutex};

            if (mCtx) {
                mCtx->TryCancel();
            }

            mCMConnected = false;
        }

        while (!mShutdown) {
            auto [credentials, err] = CreateCredentials();
            if (!err.IsNone()) {
                std::unique_lock lock {mMutex};

                LOG_ERR() << "Failed to create credentials: error=" << err;

                mCV.wait_for(lock, cReconnectTimeout, [this] { return mShutdown.load(); });

                continue;
            }

            std::lock_guard lock {mMutex};

            mCredentials = credentials;

            return;
        }
    };

    try {
        mThreadPool.start(*makeRunnable(std::move(task)));
    } catch (const Poco::Exception& e) {
        LOG_ERR() << "Failed to start cert change task: error=" << e.displayText().c_str();
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to start cert change task: error=" << e.what();
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void CMClient::Close()
{
    {
        std::lock_guard lock {mMutex};

        LOG_INF() << "Shutting down CM client";

        if (mShutdown) {
            return;
        }

        mOutgoingMsgChannel.Close();
        mIncomingMsgChannel.Close();

        if (!mNotifyConnected) {
            return;
        }

        mShutdown        = true;
        mNotifyConnected = false;

        if (mCtx) {
            mCtx->TryCancel();
        }
    }

    mCV.notify_all();

    mThreadPool.joinAll();

    if (mCMThread.joinable()) {
        mCMThread.join();
    }

    if (mHandlerOutgoingMsgsThread.joinable()) {
        mHandlerOutgoingMsgsThread.join();
    }
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> CMClient::CreateCredentials()
{
    if (mInsecureConnection) {
        return {grpc::InsecureChannelCredentials(), ErrorEnum::eNone};
    }

    iam::certhandler::CertInfo certInfo;

    return mCertProvider->GetMTLSClientCredentials(mCertStorage.c_str());
}

SMServiceStubPtr CMClient::CreateSMStub(const std::string& url)
{
    auto channel = grpc::CreateCustomChannel(url, mCredentials, grpc::ChannelArguments());
    if (!channel) {
        throw std::runtime_error("failed to create channel");
    }

    return SMService::NewStub(channel);
}

void CMClient::RegisterSM(const std::string& url)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Registering SM service: url=" << url.c_str();

    mSMStub = CreateSMStub(url);

    mCtx = std::make_unique<grpc::ClientContext>();

    if (mStream = mSMStub->RegisterSM(mCtx.get()); !mStream) {
        throw std::runtime_error("failed to register service to SM");
    }

    mCMConnected = true;
    mCV.notify_one();
}

void CMClient::RunCM(const std::string& url)
{
    LOG_DBG() << "CM client thread started";

    while (!mShutdown) {
        LOG_DBG() << "Connecting to CM...";

        try {
            RegisterSM(url);
            SendCachedMessages();
            ProcessIncomingSMMessage();

        } catch (const std::exception& e) {
            LOG_ERR() << "Failed to connect to CM: error=" << e.what();
        }

        {
            std::unique_lock lock {mMutex};

            mCMConnected = false;
            mCV.wait_for(lock, cReconnectTimeout, [&] { return mShutdown.load(); });
        }
    }

    LOG_DBG() << "CM client thread stopped";
}

void CMClient::ProcessIncomingSMMessage()
{
    LOG_DBG() << "Processing SM message";

    servicemanager::v4::SMIncomingMessages incomingMsg;

    while (mStream->Read(&incomingMsg)) {
        std::vector<uint8_t> data(incomingMsg.ByteSizeLong());

        if (!incomingMsg.SerializeToArray(data.data(), static_cast<int>(data.size()))) {
            LOG_ERR() << "Failed to serialize message";

            continue;
        }

        LOG_DBG() << "Sending message to handler";

        if (auto err = mIncomingMsgChannel.Send(std::move(data)); !err.IsNone()) {
            LOG_ERR() << "Failed to send message: error=" << err;

            return;
        }
    }
}

void CMClient::ProcessOutgoingSMMessages()
{
    LOG_DBG() << "Processing outgoing SM messages";

    while (!mShutdown) {
        auto [msg, err] = mOutgoingMsgChannel.Receive();
        if (!err.IsNone()) {
            LOG_ERR() << "Failed to receive message: error=" << err;

            return;
        }

        {
            std::unique_lock lock {mMutex};
            mCV.wait(lock, [this] { return mCMConnected || mShutdown.load(); });

            if (mShutdown) {
                return;
            }
        }

        servicemanager::v4::SMOutgoingMessages outgoingMsg;
        if (!outgoingMsg.ParseFromArray(msg.data(), static_cast<int>(msg.size()))) {
            LOG_ERR() << "Failed to parse outgoing message";

            continue;
        }

        LOG_DBG() << "Sending message to CM";

        if (!mStream->Write(outgoingMsg)) {
            LOG_ERR() << "Failed to send message";

            CacheMessage(outgoingMsg);

            continue;
        }
    }

    LOG_DBG() << "Outgoing SM messages thread stopped";
}

void CMClient::SendCachedMessages()
{
    std::lock_guard lock {mMutex};

    while (!mMessageCache.empty()) {
        const auto& message = mMessageCache.front();

        if (!mStream->Write(message)) {
            throw std::runtime_error("failed to send cached message");
        }

        mMessageCache.pop();

        LOG_DBG() << "Successfully sent cached message";
    }
}

void CMClient::CacheMessage(const servicemanager::v4::SMOutgoingMessages& message)
{
    std::lock_guard lock {mMutex};

    switch (message.SMOutgoingMessage_case()) {
    case servicemanager::v4::SMOutgoingMessages::kNodeConfigStatus:
        LOG_DBG() << "Caching NodeConfigStatus message";

        mMessageCache.push(message);

        break;

    default:
        LOG_ERR() << "Skipping caching message";

        break;
    }
}

} // namespace aos::mp::cmclient
