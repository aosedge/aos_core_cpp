/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_VISIDENTIFIER_POCOWSCLIENT_HPP_
#define AOS_IAM_VISIDENTIFIER_POCOWSCLIENT_HPP_

#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <Poco/Event.h>
#include <Poco/Net/HTTPMessage.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/WebSocket.h>

#include <core/common/crypto/crypto.hpp>

#include <iam/config/config.hpp>
#include <iam/identhandler/visidentifier/wsclient.hpp>

#include "wsclientevent.hpp"
#include "wspendingrequests.hpp"

namespace aos::iam::visidentifier {

/**
 * Poco web socket client.
 */
class PocoWSClient : public WSClientItf {
public:
    /**
     * Creates Web socket client instance.
     *
     * @param config VIS config.
     * @param uuidProvider UUID provider.
     * @param handler handler functor.
     */
    PocoWSClient(const aos::iam::config::VISIdentifierModuleParams& config, crypto::UUIDItf& uuidProvider,
        MessageHandlerFunc handler);

    /**
     * Connects to Web Socket server.
     */
    void Connect() override;

    /**
     * Closes Web Socket client.
     */
    void Close() override;

    /**
     * Disconnects Web Socket client.
     */
    void Disconnect() override;

    /**
     * Generates request id.
     *
     * @returns std::string
     */
    std::string GenerateRequestID() override;

    /**
     * Waits for Web Socket Client Event.
     *
     * @returns WSClientEvent::Details
     */
    WSClientEvent::Details WaitForEvent() override;

    /**
     * Sends request. Blocks till the response is received or timed-out (WSException is thrown).
     *
     * @param requestId request id
     * @param message request payload
     * @returns ByteArray
     */
    ByteArray SendRequest(const std::string& requestId, const ByteArray& message) override;

    /**
     * Sends message. Doesn't wait for response.
     *
     * @param message request payload
     */
    void AsyncSendMessage(const ByteArray& message) override;

    /**
     * Destroys web socket client instance.
     */
    ~PocoWSClient() override;

private:
    static constexpr Duration cDefaultTimeout = 120 * Time::cSeconds;

    void     HandleResponse(const std::string& frame);
    void     ReceiveFrames() noexcept;
    void     StartReceiveFramesThread();
    void     StopReceiveFramesThread();
    Duration GetWebSocketTimeout();

    config::VISIdentifierModuleParams              mConfig;
    crypto::UUIDItf*                               mUUIDProvider {};
    std::recursive_mutex                           mMutex;
    std::thread                                    mReceivedFramesThread;
    std::unique_ptr<Poco::Net::HTTPSClientSession> mClientSession;
    std::optional<Poco::Net::WebSocket>            mWebSocket;
    bool                                           mIsConnected {false};
    Poco::Net::HTTPRequest                         mHttpRequest;
    Poco::Net::HTTPResponse                        mHttpResponse;
    PendingRequests                                mPendingRequests;
    MessageHandlerFunc                             mHandleSubscription;
    WSClientEvent                                  mWSClientErrorEvent;
};

} // namespace aos::iam::visidentifier

#endif
