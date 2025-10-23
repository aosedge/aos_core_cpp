/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_NODESSERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_NODESSERVICE_HPP_

#include <memory>
#include <mutex>

#include <iamanager/v6/iamanager.grpc.pb.h>

#include <core/common/iamclient/itf/nodehandler.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

/**
 * Nodes service.
 */
class NodesService : public aos::iamclient::NodeHandlerItf {
public:
    /**
     * Initializes nodes service.
     *
     * @param iamProtectedServerURL IAM protected server URL.
     * @param certStorage certificate storage.
     * @param tlsCredentials TLS credentials.
     * @param insecureConnection use insecure connection.
     * @return Error.
     */
    Error Init(const std::string& iamProtectedServerURL, const std::string& certStorage,
        TLSCredentialsItf& tlsCredentials, bool insecureConnection = false);

    /**
     * Pauses node.
     *
     * @param nodeID node ID.
     * @returns Error.
     */
    Error PauseNode(const String& nodeID) override;

    /**
     * Resumes node.
     *
     * @param nodeID node ID.
     * @returns Error.
     */
    Error ResumeNode(const String& nodeID) override;

    /**
     * Reconnects to the server.
     *
     * @returns Error.
     */
    Error Reconnect();

private:
    static constexpr auto cServiceTimeout = std::chrono::seconds(10);

    std::string                                           mIAMProtectedServerURL;
    std::string                                           mCertStorage;
    bool                                                  mInsecureConnection {false};
    std::shared_ptr<grpc::ChannelCredentials>             mCredentials;
    TLSCredentialsItf*                                    mTLSCredentials {};
    std::unique_ptr<iamanager::v6::IAMNodesService::Stub> mStub;
    std::mutex                                            mMutex;
};

} // namespace aos::common::iamclient

#endif
