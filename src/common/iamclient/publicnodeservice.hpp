/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PUBLICNODESERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_PUBLICNODESERVICE_HPP_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>

#include <common/utils/grpcsubscriptionmanager.hpp>
#include <core/common/iamclient/itf/nodeinfoprovider.hpp>
#include <core/common/tools/error.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

// Type alias for NodeInfo subscription manager
using NodeInfoSubscriptionManager = utils::GRPCSubscriptionManager<iamanager::v6::IAMPublicNodesService::Stub,
    aos::iamclient::NodeInfoListenerItf, iamanager::v6::NodeInfo, NodeInfo, google::protobuf::Empty>;

/**
 * Public nodes service.
 */
class PublicNodesService : public aos::iamclient::NodeInfoProviderItf {
public:
    /**
     * Destructor
     */
    ~PublicNodesService();

    /**
     * Initializes public nodes service.
     * @param iamServerURL IAM server URL.
     * @param tlsCredentials TLS credentials.
     * @param insecureConnection whether to use insecure connection.
     * @param publicServer whether to use public connection.
     * @param certStorage certificate storage.
     * @return Error.
     */
    Error Init(const std::string& iamServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection = false,
        bool publicServer = true, const std::string& certStorage = "");

    /**
     * Returns ids for all the nodes of the unit.
     *
     * @param[out] ids result node identifiers.
     * @return Error.
     */
    Error GetAllNodeIDs(Array<StaticString<cIDLen>>& ids) const override;

    /**
     * Returns info for specified node.
     *
     * @param nodeID node identifier.
     * @param[out] nodeInfo result node information.
     * @return Error.
     */
    Error GetNodeInfo(const String& nodeID, NodeInfo& nodeInfo) const override;

    /**
     * Subscribes node info notifications.
     *
     * @param listener node info listener.
     * @return Error.
     */
    Error SubscribeListener(aos::iamclient::NodeInfoListenerItf& listener) override;

    /**
     * Unsubscribes from node info notifications.
     *
     * @param listener node info listener.
     * @return Error.
     */
    Error UnsubscribeListener(aos::iamclient::NodeInfoListenerItf& listener) override;

    /**
     * Reconnects to the server.
     * Note: Active subscription will be reconnected automatically.
     *
     * @returns Error.
     */
    Error Reconnect();

    /**
     * Start node registration.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stop node registration.
     */
    void Stop();

    /**
     * Send message.
     *
     * @param message Message.
     * @return Error.
     */
    Error SendMessage(const iamanager::v6::IAMOutgoingMessages& message);

protected:
    virtual Error ReceiveMessage(const iamanager::v6::IAMIncomingMessages& msg);
    virtual void  OnConnected();
    virtual void  OnDisconnected();

private:
    static constexpr auto cServiceTimeout    = std::chrono::seconds(10);
    static constexpr auto cReconnectInterval = std::chrono::seconds(3);

    void                                                    ConnectionLoop();
    Error                                                   RegisterNode();
    Error                                                   HandleIncomingMessage();
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> CreateCredential();

    std::string                                                 mIAMPublicServerURL;
    bool                                                        mInsecureConnection {false};
    bool                                                        mPublicServer {true};
    std::string                                                 mCertStorage;
    std::shared_ptr<grpc::ChannelCredentials>                   mCredentials;
    std::unique_ptr<iamanager::v6::IAMPublicNodesService::Stub> mStub;
    TLSCredentialsItf*                                          mTLSCredentials {};
    mutable std::mutex                                          mMutex;
    std::unique_ptr<NodeInfoSubscriptionManager>                mSubscriptionManager;

    std::unique_ptr<grpc::ClientContext> mRegisterNodeCtx;
    std::unique_ptr<
        grpc::ClientReaderWriterInterface<iamanager::v6::IAMOutgoingMessages, iamanager::v6::IAMIncomingMessages>>
                mStream;
    std::thread mConnectionThread;

    std::atomic<bool>       mStop {false};
    bool                    mConnected {false};
    bool                    mStart {false};
    std::condition_variable mCV;
};

} // namespace aos::common::iamclient

#endif
