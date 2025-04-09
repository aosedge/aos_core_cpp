/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOCKET_HPP_
#define SOCKET_HPP_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketNotification.h>
#include <Poco/Net/SocketReactor.h>
#include <Poco/Net/StreamSocket.h>

#include "types.hpp"

namespace aos::mp::communication {

/**
 * Socket class.
 */
class Socket : public TransportItf {
public:
    /**
     * Initializes the socket.
     *
     * @param port Port
     * @return Error
     */
    Error Init(int port);

    /**
     * Connects to the socket.
     *
     * @return Error
     */
    Error Connect() override;

    /**
     * Reads message from the socket.
     *
     * @param message Message
     * @return Error
     */
    Error Read(std::vector<uint8_t>& message) override;

    /**
     * Writes message to the socket.
     *
     * @param message Message
     * @return Error
     */
    Error Write(std::vector<uint8_t> message) override;

    /**
     * Closes the socket.
     *
     * @return Error
     */
    Error Close() override;

    /**
     * Shuts down socket.
     *
     * @return Error
     */
    Error Shutdown() override;

private:
    void OnAccept(Poco::Net::ReadableNotification* pNf);
    void CleanupResources();
    void ReactorThread();

    int                                     mPort {-1};
    std::atomic<bool>                       mClose {};
    std::atomic<bool>                       mShutdown {};
    bool                                    mConnectionAccepted {};
    Poco::Net::ServerSocket                 mServerSocket;
    Poco::Net::StreamSocket                 mClientSocket;
    std::optional<Poco::Net::SocketReactor> mReactor;
    std::thread                             mReactorThread;
    std::mutex                              mMutex;
    std::condition_variable                 mCV;
};

} // namespace aos::mp::communication

#endif // SOCKET_HPP_
