/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Net/SocketAddress.h>

#include "logger/logmodule.hpp"

#include "socket.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Socket::Init(int port)
{
    LOG_DBG() << "Initializing socket with: port=" << port;

    mPort = port;

    try {
        mServerSocket.bind(Poco::Net::SocketAddress("0.0.0.0", mPort), true, true);
        mServerSocket.listen(1);

        mReactor.addEventHandler(
            mServerSocket, Poco::Observer<Socket, Poco::Net::ReadableNotification>(*this, &Socket::OnAccept));

        mReactorThread = std::thread(&Socket::ReactorThread, this);

        LOG_DBG() << "Socket initialized and listening on: port=" << mPort;
    } catch (const Poco::Exception& e) {
        return Error {ErrorEnum::eRuntime, e.displayText().c_str()};
    }

    return ErrorEnum::eNone;
}

Error Socket::Connect()
{
    if (mShutdown) {
        return ErrorEnum::eFailed;
    }

    std::unique_lock lock {mMutex};

    LOG_DBG() << "Waiting for client connection";

    mCV.wait(lock, [this] { return mConnectionAccepted || mShutdown; });

    mConnectionAccepted = false;

    if (mShutdown) {
        return Error {EINTR};
    }

    return ErrorEnum::eNone;
}

Error Socket::Close()
{
    LOG_DBG() << "Closing current connection";

    if (mShutdown) {
        return ErrorEnum::eNone;
    }

    mShutdown = true;
    mReactor.stop();

    if (mReactorThread.joinable()) {
        mReactorThread.join();
    }

    mReactor.removeEventHandler(
        mServerSocket, Poco::Observer<Socket, Poco::Net::ReadableNotification>(*this, &Socket::OnAccept));

    try {
        if (mClientSocket.impl()->initialized()) {
            mClientSocket.shutdown();
            mClientSocket.close();
        }

        mServerSocket.close();
    } catch (const Poco::Exception& e) {
        return Error {ErrorEnum::eRuntime, e.displayText().c_str()};
    }

    mCV.notify_all();

    return ErrorEnum::eNone;
}

Error Socket::Read(std::vector<uint8_t>& message)
{
    LOG_DBG() << "Read from client: expectedSize=" << message.size();

    try {
        int totalRead = 0;
        while (totalRead < static_cast<int>(message.size())) {
            int bytesRead = mClientSocket.receiveBytes(message.data() + totalRead, message.size() - totalRead);
            if (bytesRead == 0) {
                return Error {ECONNRESET};
            }

            totalRead += bytesRead;
        }

        LOG_DBG() << "Total read: totalRead=" << totalRead << ", size=" << message.size();
    } catch (const Poco::Exception& e) {
        return Error {ErrorEnum::eRuntime, e.displayText().c_str()};
    }

    return ErrorEnum::eNone;
}

Error Socket::Write(std::vector<uint8_t> message)
{
    LOG_DBG() << "Write to client: size=" << message.size();

    try {
        int totalSent = 0;
        while (totalSent < static_cast<int>(message.size())) {
            int bytesSent = mClientSocket.sendBytes(message.data() + totalSent, message.size() - totalSent);
            if (bytesSent == 0) {
                return Error {ECONNRESET};
            }

            totalSent += bytesSent;
        }

        LOG_DBG() << "Total written: totalSent=" << totalSent << ", messageSize=" << message.size();
    } catch (const Poco::Exception& e) {
        return Error {ErrorEnum::eRuntime, e.displayText().c_str()};
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Socket::ReactorThread()
{
    while (!mShutdown) {
        try {
            mReactor.run();
        } catch (const Poco::Exception& e) {
            if (!mShutdown) {
                LOG_ERR() << "Reactor error: error=" << e.displayText().c_str();
            }
        }
    }
}

void Socket::OnAccept([[maybe_unused]] Poco::Net::ReadableNotification* pNf)
{
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
