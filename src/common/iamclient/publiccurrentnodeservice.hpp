/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PUBLICCERRENTNODESERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_PUBLICCERRENTNODESERVICE_HPP_

#include <memory>
#include <mutex>
#include <string>

#include <iamanager/v6/iamanager.grpc.pb.h>

#include <common/utils/grpcsubscriptionmanager.hpp>
#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/tools/error.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

// Type alias for NodeInfo subscription manager
using NodeInfoSubscriptionManager = utils::GRPCSubscriptionManager<iamanager::v6::IAMPublicCurrentNodeService::Stub,
    aos::iamclient::CurrentNodeInfoListenerItf, iamanager::v6::NodeInfo, NodeInfo, google::protobuf::Empty>;

/**
 * Public current node service.
 */
class PublicCurrentNodeService : public aos::iamclient::CurrentNodeInfoProviderItf {
public:
    /**
     * Destructor
     */
    ~PublicCurrentNodeService();

    /**
     * Initializes service.
     *
     * @param iamPublicServerURL IAM public server URL.
     * @return Error.
     */
    Error Init(
        const std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection = false);

    /**
     * Returns current node info.
     *
     * @param[out] nodeInfo current node information.
     * @return Error.
     */
    Error GetCurrentNodeInfo(NodeInfo& nodeInfo) const override;

    /**
     * Subscribes current node info notifications.
     *
     * @param listener current node info listener.
     * @return Error.
     */
    Error SubscribeListener(aos::iamclient::CurrentNodeInfoListenerItf& listener) override;

    /**
     * Unsubscribes from current node info notifications.
     *
     * @param listener current node info listener.
     * @return Error.
     */
    Error UnsubscribeListener(aos::iamclient::CurrentNodeInfoListenerItf& listener) override;

    /**
     * Reconnects to the server.
     * Note: Active subscription will be reconnected automatically.
     *
     * @returns Error.
     */
    Error Reconnect();

private:
    static constexpr auto cServiceTimeout = std::chrono::seconds(10);

    std::string                                                       mIAMPublicServerURL;
    bool                                                              mInsecureConnection {false};
    std::shared_ptr<grpc::ChannelCredentials>                         mCredentials;
    std::unique_ptr<iamanager::v6::IAMPublicCurrentNodeService::Stub> mStub;
    TLSCredentialsItf*                                                mTLSCredentials {};
    mutable std::mutex                                                mMutex;
    std::unique_ptr<NodeInfoSubscriptionManager>                      mSubscriptionManager;
};

} // namespace aos::common::iamclient

#endif
