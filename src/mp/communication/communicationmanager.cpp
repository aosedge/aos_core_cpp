/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <openssl/sha.h>

#include <common/logger/logmodule.hpp>

#include "communicationmanager.hpp"
#include "securechannel.hpp"
#include "utils.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static void CalculateChecksum(const std::vector<uint8_t>& data, uint8_t* checksum)
{
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.data(), data.size());
    SHA256_Final(checksum, &sha256);
}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CommunicationManager::Init(const config::Config& cfg, TransportItf& transport, crypto::CertLoaderItf* certLoader,
    crypto::x509::ProviderItf* cryptoProvider)
{
    LOG_DBG() << "Init communication manager";

    mTransport      = &transport;
    mCertLoader     = certLoader;
    mCryptoProvider = cryptoProvider;
    mCfg            = &cfg;

    return ErrorEnum::eNone;
}

Error CommunicationManager::Start()
{
    mThread = std::thread(&CommunicationManager::Run, this);

    return ErrorEnum::eNone;
}

Error CommunicationManager::Stop()
{
    Error err;

    {
        std::lock_guard lock {mMutex};

        if (mShutdown) {
            return ErrorEnum::eNone;
        }

        LOG_DBG() << "Close communication manager";

        mShutdown = true;
        err       = mTransport->Shutdown();
        mCondVar.notify_all();

        mIsConnected = false;
    }

    if (mThread.joinable()) {
        mThread.join();
    }

    return err;
}

std::shared_ptr<CommChannelItf> CommunicationManager::CreateChannel(
    int port, common::iamclient::TLSCredentialsItf* certProvider, const std::string& certStorage)
{
    auto chan = std::make_shared<CommunicationChannel>(port, this);

    if (certProvider == nullptr) {
        LOG_DBG() << "Create open channel";

        mChannels[port] = chan;

        return chan;
    }

    LOG_DBG() << "Create secure channel: port=" << port << ", certStorage=" << certStorage.c_str();

    auto securechannel = std::make_shared<SecureChannel>(
        *mCfg, *chan, *certProvider, *mCertLoader, *mCryptoProvider, port, certStorage);

    mChannels[port] = std::move(chan);

    return securechannel;
}

Error CommunicationManager::Connect()
{
    {
        std::lock_guard lock {mMutex};

        if (mShutdown) {
            return ErrorEnum::eFailed;
        }

        if (mIsConnected) {
            return ErrorEnum::eNone;
        }

        LOG_DBG() << "Connect communication manager";

        auto err = mTransport->Connect();
        if (!err.IsNone()) {
            return err;
        }

        mIsConnected = true;
    }

    mCondVar.notify_all();

    return ErrorEnum::eNone;
}

Error CommunicationManager::Read([[maybe_unused]] std::vector<uint8_t>& message)
{
    return ErrorEnum::eNotSupported;
}

Error CommunicationManager::Write(std::vector<uint8_t> message)
{
    std::unique_lock lock {mMutex};

    mCondVar.wait_for(lock, cConnectionTimeout, [this]() { return mIsConnected.load() || mShutdown.load(); });

    if (mShutdown) {
        return ErrorEnum::eFailed;
    }

    if (!mIsConnected) {
        return ErrorEnum::eTimeout;
    }

    return mTransport->Write(std::move(message));
}

Error CommunicationManager::Close()
{
    std::lock_guard lock {mMutex};

    if (!mIsConnected) {
        return ErrorEnum::eNone;
    }

    auto err = mTransport->Close();

    mCondVar.notify_all();

    mIsConnected = false;

    return err;
}

bool CommunicationManager::IsConnected() const
{
    return mIsConnected;
}

void CommunicationManager::OnCertChanged([[maybe_unused]] const CertInfo& info)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Certificate changed";

    mIsConnected = false;

    if (auto err = mTransport->Close(); !err.IsNone()) {
        LOG_ERR() << "Failed to close transport: error=" << err;
    }

    mCondVar.notify_all();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void CommunicationManager::Run()
{
    LOG_DBG() << "Run communication manager";

    while (!mShutdown) {
        if (auto err = Connect(); !err.IsNone()) {
            std::unique_lock lock {mMutex};

            LOG_WRN() << "Failed to connect communication manager: error=" << err;

            mCondVar.wait_for(lock, cReconnectTimeout, [this]() { return mIsConnected || mShutdown; });

            continue;
        }

        if (auto err = ReadHandler(); !err.IsNone()) {
            std::lock_guard lock {mMutex};

            LOG_ERR() << "Failed to read: error=" << err;
        }

        mIsConnected = false;

        for (const auto& channel : mChannels) {
            channel.second->Close();
        }
    }
}

Error CommunicationManager::ReadHandler()
{
    LOG_DBG() << "Read handler communication manager";

    while (!mShutdown) {
        std::vector<uint8_t> headerBuffer(sizeof(AosProtocolHeader));
        auto                 err = mTransport->Read(headerBuffer);

        if (!err.IsNone()) {
            return err;
        }

        AosProtocolHeader header;

        std::memcpy(&header, headerBuffer.data(), sizeof(AosProtocolHeader));

        int port = header.mPort;

        if (header.mDataSize > cMaxMessageSize) {
            LOG_ERR() << "Message size too big: port=" << port << ", size=" << header.mDataSize;

            continue;
        }

        std::vector<uint8_t> message(header.mDataSize);

        err = mTransport->Read(message);
        if (!err.IsNone()) {
            return err;
        }

        LOG_DBG() << "Received message: port=" << port << ", size=" << message.size();

        std::array<uint8_t, SHA256_DIGEST_LENGTH> checksum;

        CalculateChecksum(message, checksum.data());

        if (std::memcmp(checksum.data(), header.mCheckSum, SHA256_DIGEST_LENGTH) != 0) {
            LOG_ERR() << "Checksum mismatch";

            continue;
        }

        if (mChannels.find(port) == mChannels.end()) {
            LOG_ERR() << "Channel not found: port=" << port;

            continue;
        }

        if (err = mChannels[port]->Receive(std::move(message)); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

} // namespace aos::mp::communication
