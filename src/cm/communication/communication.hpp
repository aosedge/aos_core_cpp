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
#include <Poco/ThreadPool.h>
#include <Poco/URI.h>

#include <core/cm/communication/itf/communication.hpp>
#include <core/cm/communication/servicediscovery.hpp>
#include <core/common/cloudconnection/itf/cloudconnection.hpp>
#include <core/common/iamclient/itf/certhandler.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/iamclient/itf/identprovider.hpp>
#include <core/common/iamclient/itf/provisioning.hpp>
#include <core/common/tools/error.hpp>
#include <core/common/types/certificates.hpp>
#include <core/common/types/provisioning.hpp>
#include <core/iam/certhandler/certhandler.hpp>
#include <core/iam/nodeinfoprovider/itf/nodeinfoprovider.hpp>

#include <core/cm/launcher/itf/envvarhandler.hpp>
#include <core/cm/smcontroller/itf/logprovider.hpp>
#include <core/cm/storagestate/itf/statehandler.hpp>
#include <core/cm/updatemanager/itf/updatemanager.hpp>

#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include <cm/config/config.hpp>

#include "cloudprotocol/status.hpp"

namespace aos::cm::communication {

/**
 * Communication interface implementation.
 */
class Communication : public cm::communication::CommunicationItf, private NonCopyable {
public:
    /**
     * Initializes communication object.
     *
     * @param config configuration.
     * @param nodeInfoProvider node info provider.
     * @param identityProvider identity provider.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param uuidProvider UUID provider.
     * @param updateManager update manager.
     * @param stateHandler storage state handler.
     * @param logProvider log provider.
     * @param envVarHandler environment variable handler.
     * @param certHandler certificate handler.
     * @param provisioningHandler provisioning handler.
     * @return Error.
     */
    Error Init(const cm::config::Config& config, iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
        iamclient::IdentProviderItf& identityProvider, iamclient::CertProviderItf& certProvider,
        crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider, crypto::UUIDItf& uuidProvider,
        updatemanager::UpdateManagerItf& updateManager, storagestate::StateHandlerItf& stateHandler,
        smcontroller::LogProviderItf& logProvider, launcher::EnvVarHandlerItf& envVarHandler,
        iamclient::CertHandlerItf& certHandler, iamclient::ProvisioningItf& provisioningHandler

    );

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
     * Sends alerts.
     *
     * @param alerts alerts.
     * @return Error.
     */
    Error SendAlerts(const Alerts& alerts) override;

    /**
     * Sends override env vars statuses.
     *
     * @param statuses override env vars statuses.
     * @return Error.
     */
    Error SendOverrideEnvsStatuses(const OverrideEnvVarsStatuses& statuses) override;

    /**
     * Returns blobs info.
     *
     * @param digests list of blob digests.
     * @param[out] blobsInfo blobs info.
     * @return Error.
     */
    Error GetBlobsInfos(const Array<StaticString<oci::cDigestLen>>& digests, Array<BlobInfo>& blobsInfo) override;

    /**
     * Sends monitoring.
     *
     * @param monitoring monitoring.
     * @return Error.
     */
    Error SendMonitoring(const Monitoring& monitoring) override;

    /**
     * Sends log.
     *
     * @param log log message.
     * @return Error.
     */
    Error SendLog(const PushLog& log) override;

    /**
     * Sends state request for the instance.
     *
     * @param request state request.
     * @return Error.
     */
    Error SendStateRequest(const StateRequest& request) override;

    /**
     * Sends instance's new state.
     *
     * @param state new state.
     * @return Error.
     */
    Error SendNewState(const NewState& state) override;

    /**
     * Sends unit status.
     *
     * @param unitStatus unit status.
     * @return Error.
     */
    Error SendUnitStatus(const UnitStatus& unitStatus) override;

    /**
     * Subscribes to cloud connection events.
     *
     * @param listener listener reference.
     */
    Error SubscribeListener(cloudconnection::ConnectionListenerItf& listener) override;

    /**
     * Unsubscribes from cloud connection events.
     *
     * @param listener listener reference.
     */
    Error UnsubscribeListener(cloudconnection::ConnectionListenerItf& listener) override;

private:
    static constexpr auto cProtocolVersion       = 7;
    static constexpr auto cReconnectTries        = 5;
    static constexpr auto cReconnectTimeout      = Time::cSeconds * 1;
    static constexpr auto cMaxReconnectTimeout   = 10 * Time::cMinutes;
    static constexpr auto cMessageHandlerThreads = 4;

    using SessionPtr                = std::unique_ptr<Poco::Net::HTTPClientSession>;
    using ResponseMessageVariant    = std::variant<BlobURLsInfo>;
    using ResponseMessageVariantPtr = std::shared_ptr<ResponseMessageVariant>;
    using OnResponseReceivedFunc    = std::function<void(ResponseMessageVariantPtr)>;

    class Message {
    public:
        Message(const std::string& txn, Poco::JSON::Object::Ptr payload, const std::string& correlationID = {},
            const Time& timestamp = Time::Now())
            : mTxn(txn)
            , mPayload(std::move(payload))
            , mCorrelationID(correlationID)
            , mTimestamp(timestamp)
        {
        }

        const std::string& Txn() const { return mTxn; }
        const std::string& CorrelationID() const { return mCorrelationID; }
        std::string        Payload() const { return common::utils::Stringify(mPayload); }
        const Time&        Timestamp() const { return mTimestamp; }
        void               ResetTimestamp(const Time& time) { mTimestamp = time; }
        bool               RetryAllowed() const { return mTries < cMaxTries; }
        void               IncrementTries() { ++mTries; }

    private:
        static constexpr auto cMaxTries = 3;

        std::string             mTxn;
        Poco::JSON::Object::Ptr mPayload;
        std::string             mCorrelationID;
        Time                    mTimestamp {Time::Now()};
        size_t                  mTries {};
    };

    struct ResponseInfo {
        std::string mTxn;
        std::string mCorrelationID;
    };

    SessionPtr  CreateSession(const Poco::URI& uri);
    std::string CreateDiscoveryRequestBody() const;
    void        ReceiveDiscoveryResponse(Poco::Net::HTTPClientSession& session, Poco::Net::HTTPResponse& httpResponse);
    bool        ConnectionInfoIsSet() const;
    Error       SendDiscoveryRequest();
    Error       ConnectToCloud();
    Error       CloseConnection();
    Error       Disconnect();
    Error       ReceiveFrames();
    Error       CheckMessage(const common::utils::CaseInsensitiveObjectWrapper& message, ResponseInfo& info) const;
    void        NotifyConnectionEstablished();
    void        NotifyConnectionLost();
    void        HandleConnection();
    void        HandleSendQueue();
    void        HandleUnacknowledgedMessages();
    void        HandleReceivedMessage();
    Error       HandleMessage(const std::string& message);
    Poco::JSON::Object::Ptr CreateMessageHeader(const std::string& txn) const;

    Error GenerateUUID(std::string& uuid) const;
    Error GenerateUUID(String& uuid) const;
    Error EnqueueMessage(
        Poco::JSON::Object::Ptr data, bool important = false, OnResponseReceivedFunc onResponseReceived = {});
    Error EnqueueMessage(const Message& msg, OnResponseReceivedFunc onResponseReceived = {});
    Error DequeueMessage(const Message& msg);

    void  HandleMessage(const ResponseInfo& info, const cloudprotocol::Ack& ack);
    void  HandleMessage(const ResponseInfo& info, const cloudprotocol::Nack& nack);
    void  HandleMessage(const ResponseInfo& info, const BlobURLsInfo& urls);
    void  HandleMessage(const ResponseInfo& info, const DesiredStatus& status);
    void  HandleMessage(const ResponseInfo& info, const RequestLog& request);
    void  HandleMessage(const ResponseInfo& info, const StateAcceptance& state);
    void  HandleMessage(const ResponseInfo& info, const UpdateState& state);
    void  HandleMessage(const ResponseInfo& info, const OverrideEnvVarsRequest& request);
    void  HandleMessage(const ResponseInfo& info, const StartProvisioningRequest& request);
    void  HandleMessage(const ResponseInfo& info, const FinishProvisioningRequest& request);
    void  HandleMessage(const ResponseInfo& info, const DeprovisioningRequest& request);
    void  HandleMessage(const ResponseInfo& info, const RenewCertsNotification& notification);
    void  HandleMessage(const ResponseInfo& info, const IssuedUnitCerts& certs);
    Error SendAndWaitResponse(const Message& msg, ResponseMessageVariantPtr& response);
    Error SendAck(const std::string& correlationID);
    Error SendIssueUnitCerts(const IssueUnitCerts& certs);
    Error SendInstallUnitCertsConfirmation(const InstallUnitCertsConfirmation& confirmation);
    void  OnResponseReceived(const ResponseInfo& info, ResponseMessageVariantPtr message);

    const config::Config*                                mConfig {};
    iam::nodeinfoprovider::NodeInfoProviderItf*          mNodeInfoProvider {};
    iamclient::IdentProviderItf*                         mIdentityProvider {};
    iamclient::CertProviderItf*                          mCertProvider {};
    crypto::CertLoaderItf*                               mCertLoader {};
    crypto::x509::ProviderItf*                           mCryptoProvider {};
    crypto::UUIDItf*                                     mUUIDProvider {};
    updatemanager::UpdateManagerItf*                     mUpdateManager {};
    storagestate::StateHandlerItf*                       mStateHandler {};
    smcontroller::LogProviderItf*                        mLogProvider {};
    launcher::EnvVarHandlerItf*                          mEnvVarHandler {};
    iamclient::CertHandlerItf*                           mCertHandler {};
    iamclient::ProvisioningItf*                          mProvisioningHandler {};
    std::atomic_bool                                     mIsRunning {};
    SystemInfo                                           mSystemInfo;
    std::vector<cloudconnection::ConnectionListenerItf*> mSubscribers;
    std::optional<ServiceDiscoveryResponse>              mDiscoveryResponse;
    std::mutex                                           mMutex;
    std::condition_variable                              mCondVar;
    StaticString<cIDLen>                                 mMainNodeID;
    Duration                                             mReconnectTimeout {cReconnectTimeout};

    SessionPtr                          mClientSession;
    std::optional<Poco::Net::WebSocket> mWebSocket;

    Poco::Net::HTTPRequest  mCloudHttpRequest;
    Poco::Net::HTTPResponse mCloudHttpResponse;

    std::vector<Message>    mSendQueue;
    std::queue<std::string> mReceiveQueue;

    std::map<std::string, Message>                mSentMessages;
    std::map<std::string, OnResponseReceivedFunc> mResponseHandlers;
    std::vector<std::thread>                      mThreadPool;
};

} // namespace aos::cm::communication

#endif
