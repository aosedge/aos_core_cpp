/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include "iamclient.hpp"

namespace aos::mp::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error IAMClient::Init(const config::IAMConfig& cfg, aos::iamclient::CertProviderItf& certProvider,
    common::iamclient::TLSCredentialsItf& tlsCredentials, bool provisioningMode)
{
    mCertProvider = &certProvider;
    mCertStorage  = cfg.mCertStorage;

    const bool publicServer = mCertStorage.empty();

    LOG_DBG() << "Init IAM client: publicServer=" << publicServer << ", provisioningMode=" << provisioningMode;

    return PublicNodesService::Init(publicServer ? cfg.mIAMMainPublicServerURL : cfg.mIAMMainProtectedServerURL,
        tlsCredentials, provisioningMode, publicServer, mCertStorage);
}

Error IAMClient::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start IAM client";

    if (!mCertStorage.empty()) {
        if (auto err = mCertProvider->SubscribeListener(String(mCertStorage.c_str()), *this); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    mStarted = true;

    mOutgoingMsgThread = std::thread(&IAMClient::ProcessOutgoingMessages, this);

    return PublicNodesService::Start();
}

void IAMClient::Stop()
{
    LOG_DBG() << "Stop IAM client";

    {
        std::lock_guard lock {mMutex};

        if (!mStarted) {
            return;
        }

        mStarted = false;

        mOutgoingMsgChannel.Close();
        mIncomingMsgChannel.Close();
    }

    mCV.notify_all();

    PublicNodesService::Stop();

    if (mOutgoingMsgThread.joinable()) {
        mOutgoingMsgThread.join();
    }

    if (!mCertStorage.empty()) {
        mCertProvider->UnsubscribeListener(*this);
    }
}

Error IAMClient::SendMessages(std::vector<uint8_t> messages)
{
    LOG_DBG() << "Send message";

    return mOutgoingMsgChannel.Send(std::move(messages));
}

RetWithError<std::vector<uint8_t>> IAMClient::ReceiveMessages()
{
    LOG_DBG() << "Receive message";

    return mIncomingMsgChannel.Receive();
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

Error IAMClient::ReceiveMessage(const iamanager::v6::IAMIncomingMessages& msg)
{
    std::vector<uint8_t> message(msg.ByteSizeLong());

    LOG_DBG() << "Received message" << Log::Field("msg", msg.DebugString().c_str());

    if (!msg.SerializeToArray(message.data(), message.size())) {
        return Error(ErrorEnum::eRuntime, "failed to serialize message");
    }

    if (auto err = mIncomingMsgChannel.Send(std::move(message)); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

void IAMClient::OnConnected()
{
    LOG_DBG() << "IAM client connected";

    std::lock_guard lock {mMutex};

    mConnected = true;
    mCV.notify_all();
}

void IAMClient::OnDisconnected()
{
    LOG_DBG() << "IAM client disconnected";

    std::lock_guard lock {mMutex};

    mConnected = false;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void IAMClient::OnCertChanged([[maybe_unused]] const CertInfo& info)
{
    LOG_INF() << "Certificate changed, reconnecting";

    if (auto err = Reconnect(); !err.IsNone()) {
        LOG_ERR() << "Failed to reconnect" << Log::Field(err);
    }
}

void IAMClient::ProcessOutgoingMessages()
{
    LOG_DBG() << "Processing outgoing messages";

    while (mStarted) {
        auto [msg, err] = mOutgoingMsgChannel.Receive();
        if (!err.IsNone()) {
            LOG_ERR() << "Failed to receive message" << Log::Field(err);

            return;
        }

        {
            std::unique_lock lock {mMutex};

            LOG_DBG() << "Received message from channel";

            mCV.wait(lock, [this] { return mConnected || !mStarted; });

            if (!mStarted) {
                return;
            }

            iamanager::v6::IAMOutgoingMessages outgoingMsg;
            if (!outgoingMsg.ParseFromArray(msg.data(), static_cast<int>(msg.size()))) {
                LOG_ERR() << "Failed to parse outgoing message";

                continue;
            }

            LOG_DBG() << "Sending message: msg=" << outgoingMsg.DebugString().c_str();

            if (auto sendErr = SendMessage(outgoingMsg); !sendErr.IsNone()) {
                LOG_ERR() << "Failed to send message" << Log::Field(sendErr);

                mMessageCache.push(outgoingMsg);

                continue;
            }
        }
    }
}

} // namespace aos::mp::iamclient
