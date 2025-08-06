/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_COMMUNICATION_HPP_
#define AOS_CM_COMMUNICATION_COMMUNICATION_HPP_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Poco/Net/HTTPMessage.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/URI.h>

#include <core/cm/communication/communication.hpp>
#include <core/common/cloudprotocol/cloudprotocol.hpp>
#include <core/common/cloudprotocol/servicediscovery.hpp>
#include <core/common/connectionprovider/connectionprovider.hpp>
#include <core/common/crypto/crypto.hpp>
#include <core/common/crypto/cryptoutils.hpp>
#include <core/common/tools/error.hpp>
#include <core/iam/certhandler/certprovider.hpp>

#include <common/utils/time.hpp>

#include <cm/config/config.hpp>

namespace aos::cm::communication {

// TODO: move itf to a proper module.
/**
 * Identity provider interface.
 */
class IdentityProviderItf {
public:
    /**
     * Returns system ID.
     *
     * @param[out] systemID returned system ID.
     * @return Error.
     */
    virtual Error GetSystemID(String& systemID) = 0;

    /**
     * Destructor.
     */
    virtual ~IdentityProviderItf() = default;
};

/**
 * Message handler.
 */
class MessageHandler : public MessageHandlerItf {
public:
    /**
     * Handles received message.
     *
     * @param message received message.
     * @return Error.
     */
    Error HandleMessage(const cloudprotocol::MessageVariant& message) override;
};

/**
 * Communication interface implementation.
 */
class Communication : public cm::communication::CommunicationItf, public ConnectionPublisherItf, private NonCopyable {
public:
    /**
     * Initializes communication object.
     *
     * @param config configuration.
     * @param messageHandler message handler to process received messages.
     * @param identityProvider identity provider.
     * @return Error.
     */
    Error Init(const cm::config::Config& config, MessageHandlerItf& messageHandler,
        IdentityProviderItf& identityProvider, iam::certhandler::CertProviderItf& certProvider,
        crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider);

    /**
     * Starts communication.
     *
     * @return Error
     */
    Error Start();

    /**
     * Stops communication.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Sends cloud message.
     *
     * @param body cloud message body to send.
     * @return Error.
     */
    Error SendMessage(const cloudprotocol::MessageVariant& body) override;

    /**
     * Subscribes to cloud connection events.
     *
     * @param subscriber subscriber reference.
     */
    Error Subscribe(ConnectionSubscriberItf& subscriber) override;

    /**
     * Unsubscribes from cloud connection events.
     *
     * @param subscriber subscriber reference.
     */
    void Unsubscribe(ConnectionSubscriberItf& subscriber) override;

private:
    static constexpr auto cSendTimeout         = Time::cSeconds * 5;
    static constexpr auto cReconnectTries      = cloudprotocol::cServiceDiscoveryURLsCount;
    static constexpr auto cReconnectTimeout    = Time::cSeconds * 1;
    static constexpr auto cMaxReconnectTimeout = Time::cMinutes;

    using SessionPtr = std::unique_ptr<Poco::Net::HTTPClientSession>;

    SessionPtr  CreateSession(const Poco::URI& uri);
    std::string CreateDiscoveryRequestBody() const;
    void        ReceiveDiscoveryResponse(Poco::Net::HTTPClientSession& session, Poco::Net::HTTPResponse& httpResponse);
    Error       SendDiscoveryRequest();
    Error       ConnectToCloud();
    Error       CloseConnection();
    Error       Disconnect();
    Error       ReceiveFrames();
    Error       CheckMessage(const aos::cloudprotocol::CloudMessage& message) const;
    void        NotifyConnectionEstablished();
    void        NotifyConnectionLost();
    void        HandleConnection();
    void        HandleSendQueue();
    Error       HandleMessage(const std::string& message);
    Error       ScheduleMessage(const aos::cloudprotocol::CloudMessage& msg, bool important = false);

    const config::Config*                                  mConfig {nullptr};
    MessageHandlerItf*                                     mMessageHandler {nullptr};
    IdentityProviderItf*                                   mIdentityProvider {nullptr};
    iam::certhandler::CertProviderItf*                     mCertProvider {nullptr};
    crypto::CertLoaderItf*                                 mCertLoader {nullptr};
    crypto::x509::ProviderItf*                             mCryptoProvider {nullptr};
    std::atomic_bool                                       mIsRunning {false};
    StaticString<cSystemIDLen>                             mSystemID;
    std::vector<ConnectionSubscriberItf*>                  mSubscribers;
    std::optional<cloudprotocol::ServiceDiscoveryResponse> mDiscoveryResponse;
    std::mutex                                             mMutex;
    std::condition_variable                                mCondVar;

    SessionPtr                          mClientSession;
    std::optional<Poco::Net::WebSocket> mWebSocket;

    Poco::Net::HTTPRequest  mCloudHttpRequest;
    Poco::Net::HTTPResponse mCloudHttpResponse;
    std::thread             mConnectionThread;

    std::queue<std::string> mSendQueue;
    std::thread             mSendThread;
};

} // namespace aos::cm::communication

#endif // AOS_CM_CONFIG_CONFIG_HPP_
