/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Parser.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/Net/VerificationErrorArgs.h>

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include <common/cloudprotocol/cloudmessage.hpp>
#include <common/cloudprotocol/servicediscovery.hpp>
#include <common/logger/logmodule.hpp>
#include <common/utils/cryptohelper.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/pkcs11helper.hpp>
#include <common/utils/retry.hpp>

#include "communication.hpp"

namespace aos::cm::communication {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

bool IsSecured(const Poco::URI& uri)
{
    return uri.getScheme() == "wss" || uri.getScheme() == "https";
}

class MessageHandlerVisitor : public StaticVisitor<Error> {
public:
    Res Visit(const cloudprotocol::DesiredStatus& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received desired status message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::StateAcceptance& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received state acceptance message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::UpdateState& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received update state message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::RequestLog& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received request log message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::OverrideEnvVarsRequest& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received override environment variables request message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::RenewCertsNotification& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received renew certificates notification message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::IssuedUnitCerts& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received issued unit certificates message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::StartProvisioningRequest& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received start provisioning request message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::FinishProvisioningRequest& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received finish provisioning request message";

        return ErrorEnum::eNone;
    }

    Res Visit(const cloudprotocol::DeprovisioningRequest& msg) const
    {
        (void)msg;
        LOG_DBG() << "Received deprovisioning request message";

        return ErrorEnum::eNone;
    }

    template <typename T>
    Res Visit(const T&) const
    {
        return ErrorEnum::eNotSupported;
    }
};

class ImportanceVisitor : public StaticVisitor<bool> {
public:
    template <typename T>
    Res Visit(const T&) const
    {
        if constexpr (std::is_same_v<T, aos::cloudprotocol::StateRequest>
            || std::is_same_v<T, aos::cloudprotocol::PushLog> || std::is_same_v<T, aos::cloudprotocol::Alerts>
            || std::is_same_v<T, aos::cloudprotocol::IssueCertData>
            || std::is_same_v<T, aos::cloudprotocol::InstallCertData>
            || std::is_same_v<T, aos::cloudprotocol::OverrideEnvVarsStatuses>
            || std::is_same_v<T, aos::cloudprotocol::StartProvisioningResponse>
            || std::is_same_v<T, aos::cloudprotocol::FinishProvisioningResponse>
            || std::is_same_v<T, aos::cloudprotocol::DeprovisioningResponse>) {
            return true;
        }

        return false;
    }
};

} // namespace

/***********************************************************************************************************************
 * MessageHandler
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error MessageHandler::HandleMessage(const cloudprotocol::MessageVariant& message)
{
    MessageHandlerVisitor visitor;

    return message.ApplyVisitor(visitor);
}

/***********************************************************************************************************************
 * Communication
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Communication::Init(const config::Config& config, MessageHandlerItf& messageHandler,
    IdentityProviderItf& identityProvider, iam::certhandler::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    LOG_DBG() << "Initializing communication";

    Poco::Net::initializeSSL();

    mConfig           = &config;
    mMessageHandler   = &messageHandler;
    mIdentityProvider = &identityProvider;
    mCertProvider     = &certProvider;
    mCertLoader       = &certLoader;
    mCryptoProvider   = &cryptoProvider;

    mCloudHttpRequest.setMethod(Poco::Net::HTTPRequest::HTTP_GET);
    mCloudHttpRequest.setVersion(Poco::Net::HTTPMessage::HTTP_1_1);

    return ErrorEnum::eNone;
}

Error Communication::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Starting communication";

    if (mIsRunning) {
        return AOS_ERROR_WRAP(ErrorEnum::eWrongState);
    }

    try {
        if (auto err = mIdentityProvider->GetSystemID(mSystemID); !err.IsNone()) {
            return err;
        }

        mIsRunning = true;

        mConnectionThread = std::thread(&Communication::HandleConnection, this);
        mSendThread       = std::thread(&Communication::HandleSendQueue, this);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Stopping communication";

        if (!mIsRunning) {
            return AOS_ERROR_WRAP(ErrorEnum::eWrongState);
        }

        mIsRunning = false;

        CloseConnection();

        mCondVar.notify_all();
    }

    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }

    if (mSendThread.joinable()) {
        mSendThread.join();
    }

    {
        std::lock_guard lock {mMutex};

        mClientSession.reset();
        mWebSocket.reset();

        LOG_DBG() << "Communication stopped";
    }

    return ErrorEnum::eNone;
}

Error Communication::SendMessage(const cloudprotocol::MessageVariant& body)
{
    const auto isImportant = body.ApplyVisitor(ImportanceVisitor {});

    LOG_DBG() << "Sending cloud message" << Log::Field("isImportant", isImportant);

    auto msg = std::make_unique<aos::cloudprotocol::CloudMessage>();

    msg->mHeader = {aos::cloudprotocol::cProtocolVersion, mSystemID};
    msg->mData   = body;

    if (auto err = ScheduleMessage(*msg, isImportant); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Communication::Subscribe(ConnectionSubscriberItf& subscriber)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Subscribing to connection events";

    if (std::find(mSubscribers.begin(), mSubscribers.end(), &subscriber) != mSubscribers.end()) {
        return ErrorEnum::eAlreadyExist;
    }

    mSubscribers.push_back(&subscriber);

    return ErrorEnum::eNone;
}

void Communication::Unsubscribe(ConnectionSubscriberItf& subscriber)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Unsubscribing from connection events";

    auto it = std::find(mSubscribers.begin(), mSubscribers.end(), &subscriber);
    if (it != mSubscribers.end()) {
        mSubscribers.erase(it);
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::unique_ptr<Poco::Net::HTTPClientSession> Communication::CreateSession(const Poco::URI& uri)
{
    const auto isSecured = IsSecured(uri);

    LOG_DBG() << "Create client session" << Log::Field("uri", uri.toString().c_str())
              << Log::Field("secured", isSecured);

    if (isSecured) {
        auto certInfo = std::make_unique<iam::certhandler::CertInfo>();

        auto err = mCertProvider->GetCert(mConfig->mCertStorage.c_str(), {}, {}, *certInfo);
        AOS_ERROR_CHECK_AND_THROW(err);

        auto context = Poco::makeAuto<Poco::Net::Context>(Poco::Net::Context::TLS_CLIENT_USE, "");

        err = common::utils::ConfigureSSLContext(mConfig->mCertStorage.c_str(), mConfig->mCrypt.mCACert.c_str(),
            *mCertProvider, *mCertLoader, *mCryptoProvider, context->sslContext());
        AOS_ERROR_CHECK_AND_THROW(err);

        return std::make_unique<Poco::Net::HTTPSClientSession>(uri.getHost(), uri.getPort(), context);
    }

    return std::make_unique<Poco::Net::HTTPClientSession>(uri.getHost(), uri.getPort());
}

std::string Communication::CreateDiscoveryRequestBody() const
{
    auto discoveryRequest      = std::make_unique<cloudprotocol::ServiceDiscoveryRequest>();
    discoveryRequest->mVersion = cloudprotocol::cProtocolVersion;

    auto err = discoveryRequest->mSystemID.Assign(mSystemID);
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to assign system ID");

    err = discoveryRequest->mSupportedProtocols.PushBack("wss");
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to add supported protocol");

    auto requestJSON = Poco::makeShared<Poco::JSON::Object>();

    err = common::cloudprotocol::ToJSON(*discoveryRequest, *requestJSON);
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to convert discovery request to JSON");

    return common::utils::Stringify(*requestJSON);
}

void Communication::ReceiveDiscoveryResponse(
    Poco::Net::HTTPClientSession& session, Poco::Net::HTTPResponse& httpResponse)
{
    std::string responseBody;
    Poco::StreamCopier::copyToString(session.receiveResponse(httpResponse), responseBody);

    if (httpResponse.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "Discovery request failed");
    }

    auto parseResult = common::utils::ParseJson(responseBody);
    AOS_ERROR_CHECK_AND_THROW(parseResult.mError, "Failed to parse discovery response");

    mDiscoveryResponse.emplace();

    auto err = common::cloudprotocol::FromJSON(
        common::utils::CaseInsensitiveObjectWrapper(parseResult.mValue), *mDiscoveryResponse);
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to convert discovery response from JSON");
}

Error Communication::SendDiscoveryRequest()
{
    try {
        auto session      = CreateSession(Poco::URI(mConfig->mServiceDiscoveryURL));
        auto clearSession = DeferRelease(session.get(), [](auto* session) {
            if (session) {
                session->reset();
            }
        });

        LOG_DBG() << "Send discovery request";

        const auto requestBody = CreateDiscoveryRequestBody();

        Poco::Net::HTTPRequest httpRequest;
        httpRequest.setMethod(Poco::Net::HTTPRequest::HTTP_POST);
        httpRequest.setVersion(Poco::Net::HTTPMessage::HTTP_1_1);
        httpRequest.setKeepAlive(false);
        httpRequest.set("Accept", "application/json");
        httpRequest.setContentType("application/json");

        Poco::Net::HTTPResponse httpResponse;
        httpRequest.setContentLength64(requestBody.length());

        session->sendRequest(httpRequest) << requestBody;

        ReceiveDiscoveryResponse(*session, httpResponse);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::ConnectToCloud()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Connecting to cloud web socket server";

    if (!mDiscoveryResponse || mDiscoveryResponse->mConnectionInfo.IsEmpty()) {
        return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
    }

    auto it = mDiscoveryResponse->mConnectionInfo.begin();
    try {
        mClientSession = CreateSession(Poco::URI(it->CStr()));

        mCloudHttpRequest.set("Authorization", std::string("Bearer ").append(mDiscoveryResponse->mAuthToken.CStr()));
        mCloudHttpRequest.set("Accept", "application/json");
        mCloudHttpRequest.set("Connection", "Upgrade");
        mCloudHttpRequest.set("Upgrade", "websocket");

        mWebSocket.emplace(Poco::Net::WebSocket(*mClientSession, mCloudHttpRequest, mCloudHttpResponse));

        mWebSocket->setKeepAlive(true);
        mWebSocket->setReceiveTimeout(0);
    } catch (const std::exception& e) {
        LOG_DBG() << "Failed to connect to web socket server" << Log::Field("url", it->CStr())
                  << Log::Field(common::utils::ToAosError(e));

        mDiscoveryResponse->mConnectionInfo.Erase(it);

        mWebSocket.reset();

        mClientSession->reset();
        mClientSession.reset();

        return common::utils::ToAosError(e);
    }

    NotifyConnectionEstablished();

    return ErrorEnum::eNone;
}

Error Communication::CloseConnection()
{
    LOG_DBG() << "Close web socket connection";

    if (!mWebSocket) {
        return ErrorEnum::eNone;
    }

    try {
        if (mWebSocket.has_value()) {
            LOG_DBG() << "Send close frame";

            mWebSocket->shutdown();
        }

        if (mClientSession) {
            mClientSession->reset();
        }

        NotifyConnectionLost();
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::Disconnect()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Disconnect from web socket server";

    auto err = CloseConnection();

    mWebSocket.reset();
    mClientSession.reset();

    return err;
}

void Communication::NotifyConnectionEstablished()
{
    LOG_DBG() << "Notifying connection established" << Log::Field("subscribersCount", mSubscribers.size());

    for (auto& subscriber : mSubscribers) {
        subscriber->OnConnect();
    }
}

void Communication::NotifyConnectionLost()
{
    LOG_DBG() << "Notifying connection lost" << Log::Field("subscribersCount", mSubscribers.size());

    for (auto& subscriber : mSubscribers) {
        subscriber->OnDisconnect();
    }
}

void Communication::HandleConnection()
{
    LOG_DBG() << "Start connection handler thread";

    while (mIsRunning) {
        if (auto err = common::utils::Retry([this]() { return SendDiscoveryRequest(); },
                [](int retryCount, Duration delay, const aos::Error& err) {
                    LOG_WRN() << "Send discovery request failed" << Log::Field("retryCount", retryCount)
                              << Log::Field("delay", delay) << Log::Field(err);
                },
                cReconnectTries, cReconnectTimeout, cMaxReconnectTimeout);
            !err.IsNone()) {
            continue;
        }

        if (auto err = common::utils::Retry([this]() { return ConnectToCloud(); },
                [](int retryCount, Duration delay, const aos::Error& err) {
                    LOG_WRN() << "Connect to cloud failed" << Log::Field("retryCount", retryCount)
                              << Log::Field("delay", delay) << Log::Field(err);
                },
                cReconnectTries, cReconnectTimeout, cMaxReconnectTimeout);
            !err.IsNone()) {
            continue;
        }

        if (auto err = ReceiveFrames(); !err.IsNone()) {
            LOG_ERR() << "Failed to receive frames" << Log::Field(err);
        }

        if (auto err = Disconnect(); !err.IsNone()) {
            LOG_ERR() << "Failed to disconnect from cloud web socket server" << Log::Field(err);
        }
    }

    LOG_DBG() << "Stop connection handler thread";
}

Error Communication::ReceiveFrames()
{
    LOG_DBG() << "Start receiving web socket frames";

    try {
        int                flags {};
        int                n {};
        Poco::Buffer<char> buffer(0);

        do {
            n = mWebSocket->receiveFrame(buffer, flags);

            if ((flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) == Poco::Net::WebSocket::FRAME_OP_CLOSE) {
                LOG_DBG() << "Received close frame, disconnecting";

                break;
            }

            LOG_DBG() << "Received WebSocket frame" << Log::Field("size", n) << Log::Field("flags", flags);

            if (n > 0) {
                std::string message(buffer.begin(), buffer.end());

                buffer.resize(0);

                if (auto err = HandleMessage(message); !err.IsNone()) {
                    LOG_ERR() << "Failed to handle message" << Log::Field(err);
                    continue;
                }
            }
        } while (flags != 0 || n != 0);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    LOG_DBG() << "Stop receiving web socket frames";

    return ErrorEnum::eNone;
}

Error Communication::CheckMessage(const aos::cloudprotocol::CloudMessage& message) const
{
    if (mSystemID != message.mHeader.mSystemID) {
        return Error(ErrorEnum::eInvalidArgument, "systemID mismatch");
    }

    if (cloudprotocol::cProtocolVersion != message.mHeader.mVersion) {
        return Error(ErrorEnum::eInvalidArgument, "header version mismatch");
    }

    return ErrorEnum::eNone;
}

void Communication::HandleSendQueue()
{
    LOG_DBG() << "Start send queue handler thread";

    while (true) {
        std::unique_lock lock {mMutex};

        mCondVar.wait(lock, [this] { return !mSendQueue.empty() || !mIsRunning; });

        if (!mIsRunning) {
            break;
        }

        if (!mWebSocket) {
            continue;
        }

        auto message = std::move(mSendQueue.front());
        mSendQueue.pop();

        try {
            const auto sentBytes
                = mWebSocket->sendFrame(message.data(), message.size(), Poco::Net::WebSocket::FRAME_TEXT);

            LOG_DBG() << "Sent message" << Log::Field("sentBytes", sentBytes) << Log::Field("message", message.c_str());
        } catch (const std::exception& e) {
            LOG_ERR() << "Failed to send message" << Log::Field(common::utils::ToAosError(e));

            continue;
        }
    }
}

Error Communication::HandleMessage(const std::string& message)
{
    LOG_DBG() << "Handling receive queue";

    auto receivedMsg = std::make_unique<aos::cloudprotocol::CloudMessage>();

    if (auto err = common::cloudprotocol::FromJSON(message, *receivedMsg); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CheckMessage(*receivedMsg); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mMessageHandler->HandleMessage(receivedMsg->mData); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Communication::ScheduleMessage(const aos::cloudprotocol::CloudMessage& msg, bool important)
{
    if (!important && !mIsRunning) {
        return AOS_ERROR_WRAP(ErrorEnum::eWrongState);
    }

    auto jsonObject = Poco::makeShared<Poco::JSON::Object>();

    if (auto err = common::cloudprotocol::ToJSON(msg, *jsonObject); !err.IsNone()) {
        return err;
    }

    auto jsonString = common::utils::Stringify(jsonObject);

    std::lock_guard lock {mMutex};

    mSendQueue.push(std::move(jsonString));
    mCondVar.notify_all();

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication
