/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "iamconnection.hpp"
#include "communication/utils.hpp"
#include "logger/logmodule.hpp"

namespace aos::mp::communication {

Error IAMConnection::Init(int port, HandlerItf& handler, CommunicationManagerItf& comManager,
    iamclient::CertProviderItf* certProvider, const std::string& certStorage)
{
    LOG_DBG() << "Init IAM connection";

    mHandler = &handler;

    try {
        LOG_DBG() << "Create IAM channel: port=" << port << ", certStorage=" << certStorage.c_str();

        mIAMCommChannel = comManager.CreateChannel(port, certProvider, certStorage);
    } catch (std::exception& e) {
        return Error(ErrorEnum::eFailed, e.what());
    }

    mConnectThread = std::thread(&IAMConnection::Run, this);

    return ErrorEnum::eNone;
}

void IAMConnection::Close()
{
    LOG_DBG() << "Close IAM connection";

    mHandler->OnDisconnected();
    mIAMCommChannel->Close();
    mShutdown = true;

    if (mConnectThread.joinable()) {
        mConnectThread.join();
    }

    LOG_DBG() << "Close IAM connection finished";
}

void IAMConnection::Run()
{
    LOG_DBG() << "Run IAM connection";

    while (!mShutdown) {
        if (auto err = mIAMCommChannel->Connect(); !err.IsNone()) {
            std::unique_lock lock {mMutex};

            LOG_ERR() << "Failed to connect to IAM: error=" << err;

            mCondVar.wait_for(lock, cConnectionTimeout, [this]() { return mShutdown.load(); });

            continue;
        }

        mHandler->OnConnected();

        auto readThread  = std::thread(&IAMConnection::ReadHandler, this);
        auto writeThread = std::thread(&IAMConnection::WriteHandler, this);

        readThread.join();
        writeThread.join();
    }

    LOG_DBG() << "Run IAM connection finished";
}

void IAMConnection::ReadHandler()
{
    LOG_DBG() << "Read handler IAM connection";

    while (!mShutdown) {
        LOG_DBG() << "Waiting for message from IAM";

        std::vector<uint8_t> message(cProtobufHeaderSize);
        if (auto err = mIAMCommChannel->Read(message); !err.IsNone()) {
            LOG_ERR() << "Failed to read from IAM: error=" << err;

            return;
        }

        LOG_DBG() << "Received message from IAM: size=" << message.size();

        auto protobufHeader = ParseProtobufHeader(message);
        message.clear();
        message.resize(protobufHeader.mDataSize);

        if (auto err = mIAMCommChannel->Read(message); !err.IsNone()) {
            LOG_ERR() << "Failed to read from IAM: error=" << err;

            return;
        }

        LOG_DBG() << "Received message from IAM: size=" << message.size();

        if (auto err = mHandler->SendMessages(std::move(message)); !err.IsNone()) {
            LOG_ERR() << "Failed to send message to IAM: error=" << err;

            return;
        }

        LOG_DBG() << "Message sent to IAM";
    }

    LOG_DBG() << "Read handler IAM connection finished";
}

void IAMConnection::WriteHandler()
{
    LOG_DBG() << "Write handler IAM connection";

    while (!mShutdown) {
        auto message = mHandler->ReceiveMessages();
        if (!message.mError.IsNone()) {
            LOG_ERR() << "Failed to receive message from IAM: error=" << message.mError;

            return;
        }

        LOG_DBG() << "Received message from IAM: size=" << message.mValue.size();

        auto header = PrepareProtobufHeader(message.mValue.size());
        header.insert(header.end(), message.mValue.begin(), message.mValue.end());

        LOG_DBG() << "Send message to IAM channel: size=" << header.size();

        if (auto err = mIAMCommChannel->Write(std::move(header)); !err.IsNone()) {
            LOG_ERR() << "Failed to write to IAM: error=" << err;

            return;
        }
    }
}

} // namespace aos::mp::communication
