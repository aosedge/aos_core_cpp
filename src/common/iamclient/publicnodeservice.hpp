/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PUBLICNODESERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_PUBLICNODESERVICE_HPP_

#include <memory>
#include <mutex>
#include <string>

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
     * @param iamPublicServerURL IAM public server URL.
     * @param tlsCredentials TLS credentials.
     * @param insecureConnection whether to use insecure connection.
     * @return Error.
     */
    Error Init(
        const std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection = false);

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

private:
    static constexpr auto cServiceTimeout = std::chrono::seconds(10);

    std::string                                                 mIAMPublicServerURL;
    bool                                                        mInsecureConnection {false};
    std::shared_ptr<grpc::ChannelCredentials>                   mCredentials;
    std::unique_ptr<iamanager::v6::IAMPublicNodesService::Stub> mStub;
    TLSCredentialsItf*                                          mTLSCredentials {};
    mutable std::mutex                                          mMutex;
    std::unique_ptr<NodeInfoSubscriptionManager>                mSubscriptionManager;
};

} // namespace aos::common::iamclient

#endif
