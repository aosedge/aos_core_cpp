/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logger/logmodule.hpp>

#include "communication/utils.hpp"
#include "iamconnection.hpp"

namespace aos::mp::communication {

Error IAMConnection::Init(int port, HandlerItf& handler, CommunicationManagerItf& comManager,
    common::iamclient::TLSCredentialsItf* certProvider, const std::string& certStorage)
{
    LOG_DBG() << "Init IAM connection";

    mHandler = &handler;

    try {
        LOG_DBG() << "Create IAM channel: port=" << port << ", certStorage=" << certStorage.c_str();

        mIAMCommChannel = comManager.CreateChannel(port, certProvider, certStorage);
    } catch (std::exception& e) {
        return Error(ErrorEnum::eFailed, e.what());
    }

    return ErrorEnum::eNone;
}

Error IAMConnection::Start()
{
    LOG_DBG() << "Start IAM connection";

    mConnectThread = std::thread(&IAMConnection::Run, this);

    return ErrorEnum::eNone;
}

Error IAMConnection::Stop()
{
    LOG_DBG() << "Stop IAM connection";

    {
        std::lock_guard lock {mMutex};

        if (mShutdown) {
            return ErrorEnum::eNone;
        }

        mShutdown = true;

        mHandler->OnDisconnected();
        mIAMCommChannel->Close();
    }

    mCondVar.notify_all();

    if (mConnectThread.joinable()) {
        mConnectThread.join();
    }

    LOG_DBG() << "Stop IAM connection finished";

    return ErrorEnum::eNone;
}

void IAMConnection::Run()
{
    LOG_DBG() << "Run IAM connection";

    auto writeThread = std::thread(&IAMConnection::WriteHandler, this);

    while (!mShutdown) {
        if (auto err = mIAMCommChannel->Connect(); !err.IsNone()) {
            std::unique_lock lock {mMutex};

            LOG_WRN() << "Failed to connect to IAM: error=" << err;

            mCondVar.wait_for(lock, cConnectionTimeout, [this]() { return mShutdown.load(); });

            continue;
        }

        mHandler->OnConnected();
        mCondVar.notify_all();

        if (auto err = ReadHandler(); !err.IsNone()) {
            LOG_ERR() << "Failed to read from IAM: error=" << err;
        }

        if (auto err = mIAMCommChannel->Close(); !err.IsNone()) {
            LOG_ERR() << "Failed to close IAM: error=" << err;
        }
    }

    writeThread.join();

    LOG_DBG() << "Run IAM connection finished";
}

Error IAMConnection::ReadHandler()
{
    LOG_DBG() << "Read handler IAM connection";

    while (!mShutdown) {
        std::vector<uint8_t> message(cProtobufHeaderSize);
        if (auto err = mIAMCommChannel->Read(message); !err.IsNone()) {
            return err;
        }

        auto protobufHeader = ParseProtobufHeader(message);
        message.clear();
        message.resize(protobufHeader.mDataSize);

        if (auto err = mIAMCommChannel->Read(message); !err.IsNone()) {
            return err;
        }

        if (auto err = mHandler->SendMessages(std::move(message)); !err.IsNone()) {
            return err;
        }

        LOG_DBG() << "Message sent to IAM";
    }

    LOG_DBG() << "Read handler IAM connection finished";

    return ErrorEnum::eNone;
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

        {
            std::unique_lock lock {mMutex};

            mCondVar.wait(lock, [this]() { return mIAMCommChannel->IsConnected() || mShutdown.load(); });
            if (mShutdown) {
                return;
            }
        }

        auto header = PrepareProtobufHeader(message.mValue.size());
        header.insert(header.end(), message.mValue.begin(), message.mValue.end());

        if (auto err = mIAMCommChannel->Write(std::move(header)); !err.IsNone()) {
            LOG_ERR() << "Failed to write to IAM: error=" << err;

            return;
        }
    }
}

} // namespace aos::mp::communication
