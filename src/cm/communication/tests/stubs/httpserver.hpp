/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_STUBS_HTTPSERVER_HPP_
#define AOS_CM_COMMUNICATION_TESTS_STUBS_HTTPSERVER_HPP_

#include <chrono>
#include <fstream>
#include <optional>
#include <thread>

#include <Poco/Base64Encoder.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/RejectCertificateHandler.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/Net/SecureServerSocket.h>
#include <Poco/SHA1Engine.h>
#include <Poco/StreamCopier.h>
#include <Poco/Util/ServerApplication.h>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>

namespace aos::cm::communication {

/**
 * Thread safe message queue.
 */
class MessageQueue {
public:
    using Ptr = std::shared_ptr<MessageQueue>;

    /**
     * Push message to the queue.
     *
     * @param message message to push.
     */
    void Push(const std::string& message)
    {
        {
            std::lock_guard lock {mMutex};

            mQueue.push(message);
        }

        mCondition.notify_one();
    }

    /**
     * Pop message from the queue.
     *
     * @param timeout timeout for waiting for a message.
     * @return std::optional<std::string>.
     */
    std::optional<std::string> Pop(const std::chrono::milliseconds& timeout = std::chrono::seconds(1))
    {
        std::unique_lock lock {mMutex};

        if (!mCondition.wait_for(lock, timeout, [this] { return !mQueue.empty(); })) {
            return std::nullopt;
        }

        std::string message = std::move(mQueue.front());
        mQueue.pop();

        return message;
    }

    /**
     * Clears the queue.
     */
    void Clear()
    {
        std::lock_guard lock {mMutex};

        while (!mQueue.empty()) {
            mQueue.pop();
        }
    }

private:
    std::queue<std::string> mQueue;
    std::mutex              mMutex;
    std::condition_variable mCondition;
};

/**
 * Discovery request handler.
 */
class DiscoveryRequestHandler : public Poco::Net::HTTPRequestHandler {
public:
    /**
     * Constructor.
     *
     * @param receivedMessages received messages.
     * @param sendMessageQueue message queue for sending messages.
     */
    explicit DiscoveryRequestHandler(MessageQueue& receivedMessages, MessageQueue& sendMessageQueue)
        : mReceivedMessages(receivedMessages)
        , mSendMessageQueue(sendMessageQueue)
    {
    }

    /**
     * Handle request.
     *
     * @param request request.
     * @param response response.
     */
    void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override
    {
        std::string requestBody;

        if (request.hasContentLength() && request.getContentLength() > 0) {
            Poco::StreamCopier::copyToString(request.stream(), requestBody);
        } else if (request.getChunkedTransferEncoding()) {
            Poco::StreamCopier::copyToString(request.stream(), requestBody);
        }

        if (auto messageOpt = mSendMessageQueue.Pop(); messageOpt.has_value()) {
            response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
            response.setContentType("application/json");
            response.setContentLength64(messageOpt->size());
            response.send() << *messageOpt;

            return;
        }

        response.setStatus(Poco::Net::HTTPResponse::HTTP_NO_CONTENT);
        response.send();
    }

private:
    MessageQueue& mReceivedMessages;
    MessageQueue& mSendMessageQueue;
};

/**
 * WebSocket request handler.
 */
class WebSocketRequestHandler : public Poco::Net::HTTPRequestHandler {
public:
    /**
     * Constructor.
     *
     * @param receivedMessages received messages.
     * @param sendMessageQueue message queue for sending messages.
     */
    WebSocketRequestHandler(MessageQueue& receivedMessages, MessageQueue& sendMessageQueue)
        : mReceivedMessages(receivedMessages)
        , mSendMessageQueue(sendMessageQueue)
    {
    }

protected:
    void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override
    {
        try {
            mWebSocket.emplace(request, response);

            int                flags;
            int                n;
            Poco::Buffer<char> buffer(0);

            mRunning      = true;
            mSenderThread = std::thread(&WebSocketRequestHandler::RunSenderThread, this);

            do {
                n = mWebSocket->receiveFrame(buffer, flags);

                if (n == 0) {
                    continue;
                } else if ((flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) == Poco::Net::WebSocket::FRAME_OP_CLOSE) {
                    mWebSocket->sendFrame(nullptr, 0, flags);

                    break;
                }

                mReceivedMessages.Push(std::string(buffer.begin(), buffer.end()));

                buffer.resize(0);
            } while (n > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE);

        } catch (const std::exception& e) {
            LOG_ERR() << "WebSocket receiver failed" << aos::Log::Field(common::utils::ToAosError(e));
        }

        mRunning = false;

        if (mSenderThread.joinable()) {
            mSenderThread.join();
        }

        mWebSocket.reset();
    }

private:
    void RunSenderThread()
    {
        try {
            while (mRunning) {
                if (auto messageOpt = mSendMessageQueue.Pop(std::chrono::milliseconds(100)); messageOpt.has_value()) {
                    mWebSocket->sendFrame(messageOpt->c_str(), messageOpt->length(), Poco::Net::WebSocket::FRAME_TEXT);

                    continue;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERR() << "WebSocket sender failed" << aos::Log::Field(common::utils::ToAosError(e));
        }
    }

    MessageQueue&                       mReceivedMessages;
    MessageQueue&                       mSendMessageQueue;
    std::atomic_bool                    mRunning {false};
    std::optional<Poco::Net::WebSocket> mWebSocket;
    std::thread                         mSenderThread;
};

/**
 * Request handler factory.
 */
class RequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
public:
    /**
     * Constructor.
     *
     * @param receivedMessages message queue for received messages.
     * @param sendMessageQueue message queue for sending messages.
     */
    RequestHandlerFactory(MessageQueue& receivedMessages, MessageQueue& sendMessageQueue)
        : mReceivedMessages(receivedMessages)
        , mSendMessageQueue(sendMessageQueue)
    {
    }

    /**
     * Creates request handler.
     *
     * @param request request.
     * @return Poco::Net::HTTPRequestHandler.
     */
    Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest& request) override
    {
        if (IsWebSocketRequest(request)) {
            return new WebSocketRequestHandler(mReceivedMessages, mSendMessageQueue);
        }

        return new DiscoveryRequestHandler(mReceivedMessages, mSendMessageQueue);
    }

private:
    MessageQueue& mReceivedMessages;
    MessageQueue& mSendMessageQueue;

    bool IsWebSocketRequest(const Poco::Net::HTTPServerRequest& request) const
    {
        auto find = request.find("Upgrade");
        return (find != request.end() && find->second == "websocket");
    }

    std::string mResultURL;
};

/**
 * HTTP server.
 */
class HTTPServer {
public:
    /**
     * Constructor.
     *
     * @param port port.
     * @param cert path to the server certificate.
     * @param key path to the server private key.
     * @param ca path to the CA certificate.
     * @param receivedMessageQueue message queue for received messages.
     * @param sendMessageQueue message queue for sending messages.
     */
    HTTPServer(int port, const std::string& cert, const std::string& key, const std::string& ca,
        MessageQueue& receivedMessageQueue, MessageQueue& sendMessageQueue)
        : mPort(port)
        , mCert(cert)
        , mKey(key)
        , mCA(ca)
        , mReceivedMessageQueue(receivedMessageQueue)
        , mSendMessageQueue(sendMessageQueue)
    {
    }

    /**
     * Starts server.
     */
    void Start()
    {
        LOG_DBG() << "Starting HTTP server" << aos::Log::Field("port", mPort);

        Poco::Event started;

        mServerThread = std::thread([this, &started]() {
            try {

                auto cert = Poco::makeShared<Poco::Net::RejectCertificateHandler>(false);

                auto context = Poco::makeAuto<Poco::Net::Context>(Poco::Net::Context::TLS_SERVER_USE, mCert, mKey, mCA);
                Poco::Net::SSLManager::instance().initializeClient(0, cert, context);

                Poco::Net::SecureServerSocket svs(Poco::Net::SocketAddress("localhost", mPort), 64, context);

                svs.setReuseAddress(false);
                svs.setReusePort(false);

                mServer.emplace(new RequestHandlerFactory(mReceivedMessageQueue, mSendMessageQueue), svs,
                    new Poco::Net::HTTPServerParams);

                started.set();

                mServer->start();
            } catch (const Poco::Exception& e) {
                std::cerr << "Failed to start HTTP server: " << e.displayText() << std::endl;
                return;
            }
        });

        started.wait();
    }

    /**
     * Stops server.
     */
    void Stop()
    {
        LOG_DBG() << "Stopping HTTP server" << aos::Log::Field("port", mPort);

        if (mServer) {
            mServer->stopAll(true);
        }

        if (mServerThread.joinable()) {
            mServerThread.join();
        }

        mServer.reset();

        LOG_DBG() << "HTTP server stopped" << aos::Log::Field("port", mPort);
    }

private:
    int                                  mPort {};
    std::string                          mCert;
    std::string                          mKey;
    std::string                          mCA;
    MessageQueue&                        mReceivedMessageQueue;
    MessageQueue&                        mSendMessageQueue;
    std::thread                          mServerThread;
    std::optional<Poco::Net::HTTPServer> mServer;
};

} // namespace aos::cm::communication

#endif
