/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WSCLIENT_HPP_
#define WSCLIENT_HPP_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "utils/time.hpp"
#include "wsclientevent.hpp"

/**
 * Web socket client interface.
 */
class WSClientItf {
public:
    using ByteArray          = std::vector<uint8_t>;
    using MessageHandlerFunc = std::function<void(const std::string&)>;

    /**
     * Connects to Web Socket server.
     */
    virtual void Connect() = 0;

    /**
     * Closes Web Socket client.
     */
    virtual void Close() = 0;

    /**
     * Disconnects Web Socket client.
     */
    virtual void Disconnect() = 0;

    /**
     * Generates request id.
     *
     * @returns std::string
     */
    virtual std::string GenerateRequestID() = 0;

    /**
     * Waits for Web Socket Client Event.
     *
     * @returns WSClientEvent::Details
     */
    virtual WSClientEvent::Details WaitForEvent() = 0;

    /**
     * Sends request. Blocks till the response is received or timed-out (WSException is thrown).
     *
     * @param requestId request id
     * @param message request payload
     * @returns ByteArray
     */
    virtual ByteArray SendRequest(const std::string& requestId, const ByteArray& message) = 0;

    /**
     * Sends message. Doesn't wait for response.
     *
     * @param message request payload
     */
    virtual void AsyncSendMessage(const ByteArray& message) = 0;

    /**
     * Destroys web socket client instance.
     */
    virtual ~WSClientItf() = default;
};

using WSClientItfPtr = std::shared_ptr<WSClientItf>;

#endif
