/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Net/SocketAddress.h>

#include <common/logger/logmodule.hpp>

#include "socket.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Socket::Init(int port)
{
    LOG_DBG() << "Init socket with: port=" << port;

    mPort = port;

    return ErrorEnum::eNone;
}

Error Socket::Connect()
{
    std::unique_lock lock {mMutex};

    if (mShutdown) {
        return Error {EINTR};
    }

    LOG_DBG() << "Init socket with: port=" << mPort;

    mClose = false;

    if (!mReactorThread.joinable()) {
        try {
            mServerSocket.bind(Poco::Net::SocketAddress("0.0.0.0", mPort), true, true);
            mServerSocket.listen(1);

            mReactor.emplace();

            mReactor->addEventHandler(
                mServerSocket, Poco::Observer<Socket, Poco::Net::ReadableNotification>(*this, &Socket::OnAccept));

            mReactorThread = std::thread(&Socket::ReactorThread, this);

            LOG_DBG() << "Socket initialized and listening on: port=" << mPort;
        } catch (const Poco::Exception& e) {
            return Error {ErrorEnum::eRuntime, e.displayText().c_str()};
        }
    }

    LOG_DBG() << "Waiting for client connection";

    mCV.wait(lock, [this] { return mConnectionAccepted || mClose || mShutdown; });

    if (mClose || mShutdown) {
        return Error {EINTR};
    }

    mConnectionAccepted = false;

    return ErrorEnum::eNone;
}

Error Socket::Close()
{
    {
        std::lock_guard lock {mMutex};

        if (mClose) {
            return ErrorEnum::eNone;
        }

        LOG_DBG() << "Closing all connections";

        mClose = true;

        if (!mReactor.has_value()) {
            return ErrorEnum::eNone;
        }

        mReactor->stop();

        if (mReactorThread.joinable()) {
            mReactorThread.join();
        }

        mReactor->removeEventHandler(
            mServerSocket, Poco::Observer<Socket, Poco::Net::ReadableNotification>(*this, &Socket::OnAccept));

        try {
            if (mClientSocket.impl()->initialized()) {
                mClientSocket.shutdown();
                mClientSocket.close();

                mClientSocket = Poco::Net::StreamSocket {};
            }

            mServerSocket.close();
            mServerSocket = Poco::Net::ServerSocket {};

        } catch (const Poco::Exception& e) {
            return Error {ErrorEnum::eRuntime, e.displayText().c_str()};
        }
    }

    mCV.notify_all();

    return ErrorEnum::eNone;
}

Error Socket::Read(std::vector<uint8_t>& message)
{
    try {
        int totalRead = 0;
        while (totalRead < static_cast<int>(message.size())) {
            int bytesRead = mClientSocket.receiveBytes(message.data() + totalRead, message.size() - totalRead);
            if (bytesRead == 0) {
                return Error {ECONNRESET};
            }

            totalRead += bytesRead;
        }
    } catch (const Poco::Exception& e) {
        return Error {ErrorEnum::eRuntime, e.displayText().c_str()};
    }

    return ErrorEnum::eNone;
}

Error Socket::Write(std::vector<uint8_t> message)
{
    try {
        int totalSent = 0;
        while (totalSent < static_cast<int>(message.size())) {
            int bytesSent = mClientSocket.sendBytes(message.data() + totalSent, message.size() - totalSent);
            if (bytesSent == 0) {
                return Error {ECONNRESET};
            }

            totalSent += bytesSent;
        }
    } catch (const Poco::Exception& e) {
        return Error {ErrorEnum::eRuntime, e.displayText().c_str()};
    }

    return ErrorEnum::eNone;
}

Error Socket::Shutdown()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Shutting down socket";

        mShutdown = true;
        mCV.notify_all();
    }

    return Close();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Socket::ReactorThread()
{
    while (!mClose) {
        try {
            mReactor->run();
        } catch (const Poco::Exception& e) {
            if (!mClose) {
                LOG_ERR() << "Reactor error: error=" << e.displayText().c_str();
            }
        }
    }
}

void Socket::OnAccept([[maybe_unused]] Poco::Net::ReadableNotification* pNf)
{
    LOG_DBG() << "Accepting client connection";

    try {
        mClientSocket = mServerSocket.acceptConnection();

        {
            std::lock_guard lock {mMutex};

            LOG_DBG() << "Client connected: address=" << mClientSocket.peerAddress().toString().c_str();

            mConnectionAccepted = true;
        }

        mCV.notify_all();
    } catch (const Poco::Exception& e) {
        LOG_ERR() << "Failed to accept connection: error=" << e.displayText().c_str();
    }
}

} // namespace aos::mp::communication
