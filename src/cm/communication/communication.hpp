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
        crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
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
    static constexpr auto cProtocolVersion     = 7;
    static constexpr auto cSendTimeout         = Time::cSeconds * 5;
    static constexpr auto cReconnectTries      = 5;
    static constexpr auto cReconnectTimeout    = Time::cSeconds * 1;
    static constexpr auto cMaxReconnectTimeout = 10 * Time::cMinutes;

    using SessionPtr             = std::unique_ptr<Poco::Net::HTTPClientSession>;
    using RecievedMessageVariant = std::variant<StateAcceptance, UpdateState>;

    SessionPtr  CreateSession(const Poco::URI& uri);
    std::string CreateDiscoveryRequestBody() const;
    void        ReceiveDiscoveryResponse(Poco::Net::HTTPClientSession& session, Poco::Net::HTTPResponse& httpResponse);
    bool        ConnectionInfoIsSet() const;
    Error       SendDiscoveryRequest();
    Error       ConnectToCloud();
    Error       CloseConnection();
    Error       Disconnect();
    Error       ReceiveFrames();
    Error       CheckMessage(const common::utils::CaseInsensitiveObjectWrapper& message) const;
    void        NotifyConnectionEstablished();
    void        NotifyConnectionLost();
    void        HandleConnection();
    void        HandleSendQueue();
    Error       HandleMessage(const std::string& message);
    Error       ScheduleMessage(Poco::JSON::Object::Ptr data, bool important = false);
    Poco::JSON::Object::Ptr CreateMessageHeader() const;

    void HandleMessage(const DesiredStatus& status);
    void HandleMessage(const RequestLog& request);
    void HandleMessage(const StateAcceptance& state);
    void HandleMessage(const UpdateState& state);
    void HandleMessage(const OverrideEnvVarsRequest& request);
    void HandleMessage(const StartProvisioningRequest& request);
    void HandleMessage(const FinishProvisioningRequest& request);
    void HandleMessage(const DeprovisioningRequest& request);
    void HandleMessage(const RenewCertsNotification& notification);
    void HandleMessage(const IssuedUnitCerts& certs);

    Error SendIssueUnitCerts(const IssueUnitCerts& certs);
    Error SendInstallUnitCertsConfirmation(const InstallUnitCertsConfirmation& confirmation);

    const config::Config*                                mConfig {};
    iam::nodeinfoprovider::NodeInfoProviderItf*          mNodeInfoProvider {};
    iamclient::IdentProviderItf*                         mIdentityProvider {};
    iamclient::CertProviderItf*                          mCertProvider {};
    crypto::CertLoaderItf*                               mCertLoader {};
    crypto::x509::ProviderItf*                           mCryptoProvider {};
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
    std::thread             mConnectionThread;

    std::queue<std::string> mSendQueue;
    std::thread             mSendThread;
};

} // namespace aos::cm::communication

#endif
