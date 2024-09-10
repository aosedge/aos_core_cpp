/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COMMUNICATION_TYPES_HPP
#define COMMUNICATION_TYPES_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include <aos/common/tools/error.hpp>

#include "iamclient/types.hpp"

namespace aos::mp::communication {

/**
 * Communication channel interface.
 */
class CommChannelItf {
public:
    /**
     * Destructor.
     */
    ~CommChannelItf() = default;

    /**
     * Connects to channel.
     *
     * @return Error.
     */
    virtual Error Connect() = 0;

    /**
     * Reads message.
     *
     * @param message Message.
     * @return Error.
     */
    virtual Error Read(std::vector<uint8_t>& message) = 0;

    /**
     * Writes message.
     *
     * @param message Message.
     * @return Error.
     */
    virtual Error Write(std::vector<uint8_t> message) = 0;

    /**
     * Closes channel.
     *
     * @return Error.
     */
    virtual Error Close() = 0;
};

/**
 * Transport interface.
 */
class TransportItf {
public:
    /**
     * Destructor.
     */
    ~TransportItf() = default;

    /**
     * Connects to channel.
     *
     * @return Error.
     */
    virtual Error Connect() = 0;

    /**
     * Reads message.
     *
     * @param message Message.
     * @return Error.
     */
    virtual Error Read(std::vector<uint8_t>& message) = 0;

    /**
     * Writes message.
     *
     * @return Error.
     */
    virtual Error Write(std::vector<uint8_t> message) = 0;

    /**
     * Closes channel.
     *
     * @return Error.
     */
    virtual Error Close() = 0;
};

/**
 * Communication manager interface.
 */
class CommunicationManagerItf : public CommChannelItf {
public:
    /**
     * Creates channel.
     *
     * @param port Port.
     * @param certProvider Certificate provider.
     * @param certStorage Certificate storage.
     * @return std::unique_ptr<CommChannelItf>.
     */
    virtual std::shared_ptr<CommChannelItf> CreateChannel(
        int port, iamclient::CertProviderItf* certProvider = nullptr, const std::string& certStorage = "")
        = 0;
};

/**
 * Handler interface.
 */
class HandlerItf {
public:
    /**
     * Destructor.
     */
    virtual ~HandlerItf() = default;

    /**
     * Notifies about connection.
     */
    virtual void OnConnected() = 0;

    /**
     * Notifies about disconnection.
     */
    virtual void OnDisconnected() = 0;

    /**
     * Sends messages.
     *
     * @param messages Messages.
     * @return Error.
     */
    virtual Error SendMessages(std::vector<uint8_t> messages) = 0;

    /**
     * Receives messages.
     *
     * @return RetWithError<std::vector<uint8_t>>.
     */
    virtual RetWithError<std::vector<uint8_t>> ReceiveMessages() = 0;
};

} // namespace aos::mp::communication

#endif // COMMUNICATION_TYPES_HPP
