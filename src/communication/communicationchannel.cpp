/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>

#include <logger/logmodule.hpp>

#include "communication/utils.hpp"
#include "communicationchannel.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::mutex CommunicationChannel::mCommChannelMutex;

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

CommunicationChannel::CommunicationChannel(int port, CommChannelItf* commChan)
    : mCommChannel(commChan)
    , mPort(port)
{
}

Error CommunicationChannel::Connect()
{
    std::unique_lock lock {mCommChannelMutex};

    mClose = false;

    LOG_DBG() << "Connect in communication channel";

    return mCommChannel->Connect();
}

bool CommunicationChannel::IsConnected() const
{
    return mCommChannel->IsConnected();
}

Error CommunicationChannel::Read(std::vector<uint8_t>& message)
{
    std::unique_lock lock {mMutex};

    LOG_DBG() << "Requesting: port=" << mPort << ", size=" << message.size();

    mCondVar.wait(lock, [this] { return !mReceivedMessage.empty() || mClose; });

    if (mClose) {
        return ErrorEnum::eRuntime;
    }

    if (mReceivedMessage.size() < message.size()) {
        return ErrorEnum::eRuntime;
    }

    message.assign(mReceivedMessage.begin(), mReceivedMessage.begin() + message.size());
    mReceivedMessage.erase(mReceivedMessage.begin(), mReceivedMessage.begin() + message.size());

    return ErrorEnum::eNone;
}

Error CommunicationChannel::Write(std::vector<uint8_t> message)
{
    {
        std::unique_lock lock {mMutex};
        if (mClose) {
            return ErrorEnum::eRuntime;
        }
    }

    std::unique_lock lock {mCommChannelMutex};

    LOG_DBG() << "Write data: port=" << mPort << ", size=" << message.size();

    auto header = PrepareHeader(mPort, message);
    if (header.empty()) {
        return Error(ErrorEnum::eRuntime, "failed to prepare header");
    }

    if (auto err = mCommChannel->Write(std::move(header)); !err.IsNone()) {
        return err;
    }

    LOG_DBG() << "Write message: msg=" << message.size();

    return mCommChannel->Write(std::move(message));
}

Error CommunicationChannel::Close()
{
    {
        std::unique_lock lock {mMutex};

        LOG_DBG() << "Close communication channel: port=" << mPort;

        if (mClose) {
            return ErrorEnum::eFailed;
        }

        mClose = true;

        mReceivedMessage.clear();
    }

    mCondVar.notify_all();

    return ErrorEnum::eNone;
}

Error CommunicationChannel::Receive(std::vector<uint8_t> message)
{
    std::unique_lock lock {mMutex};

    LOG_DBG() << "Received message: port=" << mPort << ", size=" << message.size();

    mReceivedMessage.insert(mReceivedMessage.end(), message.begin(), message.end());
    mCondVar.notify_all();

    LOG_DBG() << "Received message: size=" << mReceivedMessage.size();

    return ErrorEnum::eNone;
}

} // namespace aos::mp::communication
