/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COMMUNICATIONCHANNEL_HPP_
#define COMMUNICATIONCHANNEL_HPP_

#include <condition_variable>
#include <mutex>
#include <vector>

#include "types.hpp"

namespace aos::mp::communication {

/**
 * Communication channel class.
 */
class CommunicationChannel : public CommChannelItf {
public:
    /**
     * Constructor.
     *
     * @param port Port
     * @param commChan Communication channel
     */
    CommunicationChannel(int port, CommChannelItf* commChan);

    /**
     * Connects to communication channel.
     */
    Error Connect() override;

    /**
     * Reads message from communication channel.
     *
     * @param message Message
     * @return Error
     */
    Error Read(std::vector<uint8_t>& message) override;

    /**
     * Writes message to communication channel.
     *
     * @param message Message
     * @return Error
     */
    Error Write(std::vector<uint8_t> message) override;

    /**
     * Closes the communication channel.
     *
     * @return Error
     */
    Error Close() override;

    /**
     * Receives message.
     *
     * @param message Message
     * @return Error
     */
    Error Receive(std::vector<uint8_t> message);

    /**
     * Checks if channel is connected.
     *
     * @return bool
     */
    bool IsConnected() const override;

private:
    // Global mutex for synchronization communication channel
    static std::mutex mCommChannelMutex;

    CommChannelItf*         mCommChannel {};
    int                     mPort {-1};
    bool                    mClose {false};
    std::vector<uint8_t>    mReceivedMessage;
    std::mutex              mMutex;
    std::condition_variable mCondVar;
};

} // namespace aos::mp::communication

#endif /* COMMUNICATIONCHANNEL_HPP_ */
