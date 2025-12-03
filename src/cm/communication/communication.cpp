/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <variant>

#include <Poco/Net/NetException.h>
#include <Poco/Net/SSLManager.h>

#include <common/logger/logmodule.hpp>
#include <common/utils/cryptohelper.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/pkcs11helper.hpp>
#include <common/utils/retry.hpp>

#include "cloudprotocol/alerts.hpp"
#include "cloudprotocol/certificates.hpp"
#include "cloudprotocol/common.hpp"
#include "cloudprotocol/desiredstatus.hpp"
#include "cloudprotocol/envvars.hpp"
#include "cloudprotocol/log.hpp"
#include "cloudprotocol/monitoring.hpp"
#include "cloudprotocol/provisioning.hpp"
#include "cloudprotocol/servicediscovery.hpp"
#include "cloudprotocol/state.hpp"
#include "cloudprotocol/unitstatus.hpp"

#include "communication.hpp"

namespace aos::cm::communication {

namespace {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

using RecievedMessageVariant
    = std::variant<DesiredStatus, RequestLog, StateAcceptance, UpdateState, RenewCertsNotification, IssuedUnitCerts,
        OverrideEnvVarsRequest, StartProvisioningRequest, FinishProvisioningRequest, DeprovisioningRequest>;

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

bool IsSecured(const Poco::URI& uri)
{
    return uri.getScheme() == "wss" || uri.getScheme() == "https";
}

template <typename T>
Poco::JSON::Object::Ptr CreateMessageData(const T& data)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = cloudprotocol::ToJSON(data, *json);
    AOS_ERROR_CHECK_AND_THROW(err);

    return json;
}

std::unique_ptr<RecievedMessageVariant> ParseMessage(const common::utils::CaseInsensitiveObjectWrapper& json)
{
    auto result = std::make_unique<RecievedMessageVariant>();

    cloudprotocol::MessageType type;

    auto err = type.FromString(json.GetValue<std::string>("messageType").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse message type");

    switch (type.GetValue()) {
    case cloudprotocol::MessageTypeEnum::eDesiredStatus: {
        err = cloudprotocol::FromJSON(json, result->emplace<DesiredStatus>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eRequestLog: {
        err = cloudprotocol::FromJSON(json, result->emplace<RequestLog>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eStateAcceptance: {
        err = cloudprotocol::FromJSON(json, result->emplace<StateAcceptance>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eUpdateState: {
        err = cloudprotocol::FromJSON(json, result->emplace<UpdateState>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eRenewCertificatesNotification: {
        err = cloudprotocol::FromJSON(json, result->emplace<RenewCertsNotification>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eIssuedUnitCertificates: {
        err = cloudprotocol::FromJSON(json, result->emplace<IssuedUnitCerts>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eOverrideEnvVars: {
        err = cloudprotocol::FromJSON(json, result->emplace<OverrideEnvVarsRequest>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eStartProvisioningRequest: {
        err = cloudprotocol::FromJSON(json, result->emplace<StartProvisioningRequest>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eFinishProvisioningRequest: {
        err = cloudprotocol::FromJSON(json, result->emplace<FinishProvisioningRequest>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    case cloudprotocol::MessageTypeEnum::eDeprovisioningRequest: {
        err = cloudprotocol::FromJSON(json, result->emplace<DeprovisioningRequest>());
        AOS_ERROR_CHECK_AND_THROW(err);

        return result;
    }

    default:
        break;
    }

    AOS_ERROR_THROW(ErrorEnum::eNotSupported, "unsupported message type");
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Communication::Init(const cm::config::Config& config,
    iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider, iamclient::IdentProviderItf& identityProvider,
    iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, updatemanager::UpdateManagerItf& updateManager,
    storagestate::StateHandlerItf& stateHandler, smcontroller::LogProviderItf& logProvider,
    launcher::EnvVarHandlerItf& envVarHandler, iamclient::CertHandlerItf& certHandler,
    iamclient::ProvisioningItf& provisioningHandler)
{
    LOG_DBG() << "Initializing communication";

    Poco::Net::initializeSSL();

    mConfig              = &config;
    mNodeInfoProvider    = &nodeInfoProvider;
    mIdentityProvider    = &identityProvider;
    mCertProvider        = &certProvider;
    mCertLoader          = &certLoader;
    mCryptoProvider      = &cryptoProvider;
    mUpdateManager       = &updateManager;
    mStateHandler        = &stateHandler;
    mLogProvider         = &logProvider;
    mEnvVarHandler       = &envVarHandler;
    mCertHandler         = &certHandler;
    mProvisioningHandler = &provisioningHandler;

    mCloudHttpRequest.setMethod(Poco::Net::HTTPRequest::HTTP_GET);
    mCloudHttpRequest.setVersion(Poco::Net::HTTPMessage::HTTP_1_1);
    mCloudHttpRequest.set("Accept", "application/json");
    mCloudHttpRequest.set("Connection", "Upgrade");
    mCloudHttpRequest.set("Upgrade", "websocket");

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
        if (auto err = mIdentityProvider->GetSystemInfo(mSystemInfo); !err.IsNone()) {
            return err;
        }

        auto nodeInfo = std::make_unique<NodeInfo>();

        if (auto err = mNodeInfoProvider->GetNodeInfo(*nodeInfo); !err.IsNone()) {
            return err;
        }

        mMainNodeID = nodeInfo->mNodeID;
        mIsRunning  = true;

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

        LOG_DBG() << "Stop communication";

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

Error Communication::SendAlerts(const Alerts& alerts)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send alerts";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(alerts), true); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::SendOverrideEnvsStatuses(const OverrideEnvVarsStatuses& statuses)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send override env vars statuses";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(statuses), true); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::GetBlobsInfos(const Array<StaticString<oci::cDigestLen>>& digests, Array<BlobInfo>& blobsInfo)
{
    (void)blobsInfo;

    LOG_DBG() << "Get blobs" << Log::Field("count", digests.Size());

    return ErrorEnum::eNotSupported;
}

Error Communication::SendMonitoring(const Monitoring& monitoring)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send monitoring";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(monitoring), false); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::SendLog(const PushLog& log)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send log";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(log), true); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::SendStateRequest(const StateRequest& request)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send state request";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(request), true); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::SendNewState(const NewState& state)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send new state";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(state), false); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::SendUnitStatus(const UnitStatus& unitStatus)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send unit status";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(unitStatus), false); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::SubscribeListener(cloudconnection::ConnectionListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Subscribing connection listener";

    if (std::find(mSubscribers.begin(), mSubscribers.end(), &listener) != mSubscribers.end()) {
        return ErrorEnum::eAlreadyExist;
    }

    mSubscribers.push_back(&listener);

    return ErrorEnum::eNone;
}

Error Communication::UnsubscribeListener(cloudconnection::ConnectionListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Unsubscribing connection listener";

    if (auto it = std::find(mSubscribers.begin(), mSubscribers.end(), &listener); it != mSubscribers.end()) {
        mSubscribers.erase(it);

        return ErrorEnum::eNone;
    }

    return ErrorEnum::eNotFound;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::unique_ptr<Poco::Net::HTTPClientSession> Communication::CreateSession(const Poco::URI& uri)
{
    const auto isSecured = IsSecured(uri);

    LOG_DBG() << "Create client session" << Log::Field("uri", uri.toString().c_str())
              << Log::Field("secured", isSecured);

    if (!isSecured) {
        return std::make_unique<Poco::Net::HTTPClientSession>(uri.getHost(), uri.getPort());
    }

    auto certInfo = std::make_unique<CertInfo>();

    auto err = mCertProvider->GetCert(mConfig->mCertStorage.c_str(), {}, {}, *certInfo);
    AOS_ERROR_CHECK_AND_THROW(err);

    auto context = Poco::makeAuto<Poco::Net::Context>(Poco::Net::Context::TLS_CLIENT_USE, "");

    err = common::utils::ConfigureSSLContext(mConfig->mCertStorage.c_str(), mConfig->mCrypt.mCACert.c_str(),
        *mCertProvider, *mCertLoader, *mCryptoProvider, context->sslContext());
    AOS_ERROR_CHECK_AND_THROW(err);

    return std::make_unique<Poco::Net::HTTPSClientSession>(uri.getHost(), uri.getPort(), context);
}

std::string Communication::CreateDiscoveryRequestBody() const
{
    auto discoveryRequest      = std::make_unique<ServiceDiscoveryRequest>();
    discoveryRequest->mVersion = cProtocolVersion;

    auto err = discoveryRequest->mSystemID.Assign(mSystemInfo.mSystemID);
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to assign system ID");

    err = discoveryRequest->mSupportedProtocols.PushBack("wss");
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to add supported protocol");

    auto requestJSON = Poco::makeShared<Poco::JSON::Object>();

    err = cloudprotocol::ToJSON(*discoveryRequest, *requestJSON);
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to convert discovery request to JSON");

    return common::utils::Stringify(*requestJSON);
}

bool Communication::ConnectionInfoIsSet() const
{
    return mDiscoveryResponse && !mDiscoveryResponse->mConnectionInfo.IsEmpty();
}

void Communication::ReceiveDiscoveryResponse(
    Poco::Net::HTTPClientSession& session, Poco::Net::HTTPResponse& httpResponse)
{
    std::string responseBody;
    Poco::StreamCopier::copyToString(session.receiveResponse(httpResponse), responseBody);

    if (httpResponse.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "Discovery request failed");
    }

    mDiscoveryResponse.emplace();

    auto err = cloudprotocol::FromJSON(responseBody, *mDiscoveryResponse);
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
        httpRequest.setContentLength64(static_cast<Poco::Int64>(requestBody.length()));

        session->sendRequest(httpRequest) << requestBody;

        ReceiveDiscoveryResponse(*session, httpResponse);

    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    if (!ConnectionInfoIsSet()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eRuntime, "No connection info received"));
    }

    if (mDiscoveryResponse->mNextRequestDelay > 0) {
        mReconnectTimeout = mDiscoveryResponse->mNextRequestDelay;
    }

    return ErrorEnum::eNone;
}

Error Communication::ConnectToCloud()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Connect to cloud web socket server";

    if (!ConnectionInfoIsSet()) {
        if (auto err = SendDiscoveryRequest(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mCloudHttpRequest.set("Authorization", std::string("Bearer ").append(mDiscoveryResponse->mAuthToken.CStr()));
    }

    auto it = mDiscoveryResponse->mConnectionInfo.begin();
    try {
        mClientSession = CreateSession(Poco::URI(it->CStr()));

        mWebSocket.emplace(Poco::Net::WebSocket(*mClientSession, mCloudHttpRequest, mCloudHttpResponse));

        mWebSocket->setKeepAlive(true);
        mWebSocket->setReceiveTimeout(0);
    } catch (const Poco::Net::NetException& e) {
        if (e.code() == Poco::Net::WebSocket::WS_ERR_UNAUTHORIZED) {
            LOG_WRN() << "Authorization failed, clearing discovery response";

            mDiscoveryResponse.reset();
        }

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
    LOG_INF() << "Notifying connection established" << Log::Field("subscribersCount", mSubscribers.size());

    for (auto& subscriber : mSubscribers) {
        subscriber->OnConnect();
    }
}

void Communication::NotifyConnectionLost()
{
    LOG_INF() << "Notifying connection lost" << Log::Field("subscribersCount", mSubscribers.size());

    for (auto& subscriber : mSubscribers) {
        subscriber->OnDisconnect();
    }
}

void Communication::HandleConnection()
{
    LOG_DBG() << "Start connection handler thread";

    while (mIsRunning) {
        if (auto err = common::utils::Retry([this]() { return ConnectToCloud(); },
                [](int retryCount, Duration delay, const aos::Error& err) {
                    LOG_WRN() << "Connect to cloud failed" << Log::Field("retryCount", retryCount)
                              << Log::Field("delay", delay) << Log::Field(err);
                },
                cReconnectTries, mReconnectTimeout, cMaxReconnectTimeout);
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

Error Communication::CheckMessage(const common::utils::CaseInsensitiveObjectWrapper& message) const
{
    if (!message.Has("header")) {
        return Error(ErrorEnum::eInvalidArgument, "missing header");
    }

    if (!message.Has("data")) {
        return Error(ErrorEnum::eInvalidArgument, "missing data");
    }

    const auto header = message.GetObject("header");

    if (const auto version = header.GetValue<size_t>("version"); version != cProtocolVersion) {
        return Error(ErrorEnum::eInvalidArgument, "header version mismatch");
    }

    if (const auto systemID = header.GetValue<std::string>("systemId"); mSystemInfo.mSystemID != systemID.c_str()) {
        return Error(ErrorEnum::eInvalidArgument, "systemID mismatch");
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

        const auto& message = mSendQueue.front();

        try {
            const auto sentBytes
                = mWebSocket->sendFrame(message.data(), message.size(), Poco::Net::WebSocket::FRAME_TEXT);

            LOG_DBG() << "Sent message" << Log::Field("sentBytes", sentBytes) << Log::Field("message", message.c_str());

            mSendQueue.pop();
        } catch (const std::exception& e) {
            LOG_ERR() << "Failed to send message" << Log::Field(common::utils::ToAosError(e));

            continue;
        }
    }
}

Error Communication::HandleMessage(const std::string& message)
{
    LOG_DBG() << "Handle receive queue";

    auto [objectVar, err] = common::utils::ParseJson(message);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto object = common::utils::CaseInsensitiveObjectWrapper(objectVar);

    err = CheckMessage(object);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto data = object.GetObject("data");

    try {
        auto messageVariant = ParseMessage(data);

        std::visit([this](auto&& arg) { HandleMessage(arg); }, *messageVariant);

    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::ScheduleMessage(Poco::JSON::Object::Ptr data, bool important)
{
    if (!important && !mIsRunning) {
        return AOS_ERROR_WRAP(ErrorEnum::eWrongState);
    }

    auto msg = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    msg->set("header", CreateMessageHeader());
    msg->set("data", std::move(data));

    mSendQueue.push(common::utils::Stringify(msg));
    mCondVar.notify_all();

    return ErrorEnum::eNone;
}

Poco::JSON::Object::Ptr Communication::CreateMessageHeader() const
{
    auto header = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    header->set("version", cProtocolVersion);
    header->set("systemId", mSystemInfo.mSystemID.CStr());

    return header;
}

void Communication::HandleMessage(const DesiredStatus& status)
{
    LOG_DBG() << "Received desired status message";

    if (auto err = mUpdateManager->ProcessDesiredStatus(status); !err.IsNone()) {
        LOG_ERR() << "Desired status processing failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const RequestLog& request)
{
    LOG_DBG() << "Received log request message";

    if (auto err = mLogProvider->RequestLog(request); !err.IsNone()) {
        LOG_ERR() << "Log request failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const StateAcceptance& state)
{
    LOG_DBG() << "Received state acceptance message";

    if (auto err = mStateHandler->AcceptState(state); !err.IsNone()) {
        LOG_ERR() << "State acceptance failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const UpdateState& state)
{
    LOG_DBG() << "Received update state message";

    if (auto err = mStateHandler->UpdateState(state); !err.IsNone()) {
        LOG_ERR() << "State update failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const OverrideEnvVarsRequest& request)
{
    LOG_DBG() << "Received override env vars request message";

    if (auto err = mEnvVarHandler->OverrideEnvVars(request); !err.IsNone()) {
        LOG_ERR() << "Override env vars failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const StartProvisioningRequest& request)
{
    LOG_DBG() << "Received start provisioning request message" << Log::Field("nodeID", request.mNodeID.CStr());

    if (auto err = mProvisioningHandler->StartProvisioning(request.mNodeID, request.mPassword); !err.IsNone()) {
        LOG_ERR() << "Start provisioning failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const FinishProvisioningRequest& request)
{
    LOG_DBG() << "Received finish provisioning request message" << Log::Field("nodeID", request.mNodeID.CStr());

    if (auto err = mProvisioningHandler->FinishProvisioning(request.mNodeID, request.mPassword); !err.IsNone()) {
        LOG_ERR() << "Finish provisioning failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const DeprovisioningRequest& request)
{
    LOG_DBG() << "Received deprovisioning request message" << Log::Field("nodeID", request.mNodeID.CStr());

    if (auto err = mProvisioningHandler->Deprovision(request.mNodeID, request.mPassword); !err.IsNone()) {
        LOG_ERR() << "Deprovisioning failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const RenewCertsNotification& notification)
{
    LOG_DBG() << "Received renew certs notification message";

    if (notification.mCertificates.IsEmpty()) {
        LOG_WRN() << "No certificates to renew";
        return;
    }

    auto newCerts = std::make_unique<IssueUnitCerts>();

    for (const auto& cert : notification.mCertificates) {
        LOG_DBG() << "Renew certificate" << Log::Field("nodeID", cert.mNodeID) << Log::Field("type", cert.mType);

        const auto itSecrets = notification.mUnitSecrets.mNodes.FindIf(
            [&cert](const NodeSecret& secret) { return secret.mNodeID == cert.mNodeID; });
        if (itSecrets == notification.mUnitSecrets.mNodes.end()) {
            LOG_ERR() << "No secrets found for node" << Log::Field("nodeID", cert.mNodeID);
            return;
        }

        if (auto err = newCerts->mRequests.PushBack({cert.mType, cert.mNodeID, {}}); !err.IsNone()) {
            LOG_ERR() << "Failed to add new cert request" << Log::Field(err);
            return;
        }

        if (auto err = mCertHandler->CreateKey(
                cert.mNodeID, cert.mType.ToString(), {}, itSecrets->mSecret, newCerts->mRequests.Back().mCSR);
            !err.IsNone()) {
            LOG_ERR() << "Create key failed" << Log::Field(err);
            return;
        }
    }

    if (auto err = SendIssueUnitCerts(*newCerts); !err.IsNone()) {
        LOG_ERR() << "Send issue unit certs failed" << Log::Field(err);
    }
}

void Communication::HandleMessage(const IssuedUnitCerts& certs)
{
    LOG_DBG() << "Received issued unit certs message";

    if (certs.mCertificates.IsEmpty()) {
        LOG_WRN() << "No issued certificates received";
        return;
    }

    auto confirmation = std::make_unique<InstallUnitCertsConfirmation>();

    std::vector<IssuedCertData> issuedCerts(certs.mCertificates.begin(), certs.mCertificates.end());

    // IAM cert type of secondary nodes should be sent the latest among certificates for that node.
    // And IAM certificate for the main node should be send in the end. Otherwise IAM client/server
    // restart will fail the following certificates to apply.
    std::stable_sort(issuedCerts.begin(), issuedCerts.end(), [&](const IssuedCertData& a, const IssuedCertData& b) {
        // IAM cert for main node should go last
        if (a.mNodeID == mMainNodeID && a.mType == CertTypeEnum::eIAM) {
            return false;
        }

        if (b.mNodeID == mMainNodeID && b.mType == CertTypeEnum::eIAM) {
            return true;
        }

        // Main node certs should go last
        if (a.mNodeID == mMainNodeID) {
            return false;
        }

        if (b.mNodeID == mMainNodeID) {
            return true;
        }

        if (a.mNodeID == b.mNodeID) {
            // IAM cert should be last for the node
            if (a.mType == CertTypeEnum::eIAM) {
                return false;
            }

            if (b.mType == CertTypeEnum::eIAM) {
                return true;
            }

            return false;
        }

        return a.mNodeID < b.mNodeID;
    });

    for (const auto& cert : issuedCerts) {
        LOG_DBG() << "Install certificate" << Log::Field("nodeID", cert.mNodeID) << Log::Field("type", cert.mType);

        if (auto err = confirmation->mCertificates.PushBack({cert.mType, cert.mNodeID, {}, {}}); !err.IsNone()) {
            LOG_ERR() << "Failed to add new cert confirmation" << Log::Field(err);
            continue;
        }

        auto certInfo = std::make_unique<CertInfo>();

        if (auto err = mCertHandler->ApplyCert(cert.mNodeID, cert.mType.ToString(), cert.mCertificateChain, *certInfo);
            !err.IsNone()) {
            LOG_ERR() << "Apply certificate failed" << Log::Field(err);

            confirmation->mCertificates.Back().mError = err;
            continue;
        }

        if (auto err = confirmation->mCertificates.Back().mSerial.ByteArrayToHex(certInfo->mSerial); !err.IsNone()) {
            LOG_ERR() << "Convert serial to hex failed" << Log::Field(err);

            confirmation->mCertificates.Back().mError = err;
            continue;
        }
    }

    if (auto err = SendInstallUnitCertsConfirmation(*confirmation); !err.IsNone()) {
        LOG_ERR() << "Send install unit certs confirmation failed" << Log::Field(err);
    }
}

Error Communication::SendIssueUnitCerts(const IssueUnitCerts& certs)
{
    LOG_DBG() << "Send issue unit certs";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(certs), true); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Communication::SendInstallUnitCertsConfirmation(const InstallUnitCertsConfirmation& confirmation)
{
    LOG_DBG() << "Send install unit certs confirmation";

    try {
        if (auto err = ScheduleMessage(CreateMessageData(confirmation), true); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication
