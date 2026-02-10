/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_IAMCLIENT_IAMCLIENT_HPP_
#define AOS_MP_IAMCLIENT_IAMCLIENT_HPP_

#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/tools/error.hpp>

#include <iamanager/v6/iamanager.grpc.pb.h>

#include <common/iamclient/publicnodeservice.hpp>
#include <common/utils/channel.hpp>
#include <mp/config/config.hpp>

namespace aos::mp::iamclient {

/**
 * IAM client.
 */
class IAMClient : public common::iamclient::PublicNodesService, private aos::iamclient::CertListenerItf {
public:
    /**
     * Initializes the client.
     *
     * @param cfg Configuration.
     * @param certProvider Certificate provider.
     * @param tlsCredentials TLS credentials.
     * @param provisioningMode Whether the node is in provisioning mode (uses insecure connection).
     * @return Error error code.
     */
    Error Init(const config::IAMConfig& cfg, aos::iamclient::CertProviderItf& certProvider,
        common::iamclient::TLSCredentialsItf& tlsCredentials, bool provisioningMode = false);

    /**
     * Starts the client.
     *
     * @return Error error code.
     */
    Error Start();

    /**
     * Stops the client.
     */
    void Stop();

    /**
     * Sends messages.
     *
     * @param messages Messages.
     * @return Error error code.
     */
    Error SendMessages(std::vector<uint8_t> messages);

    /**
     * Receives messages.
     *
     * @return Messages.
     */
    RetWithError<std::vector<uint8_t>> ReceiveMessages();

protected:
    Error ReceiveMessage(const iamanager::v6::IAMIncomingMessages& msg) override;
    void  OnConnected() override;
    void  OnDisconnected() override;

private:
    void OnCertChanged(const CertInfo& info) override;
    void ProcessOutgoingMessages();

    aos::iamclient::CertProviderItf* mCertProvider {};
    std::string                      mCertStorage;

    std::thread             mOutgoingMsgThread;
    std::mutex              mMutex;
    std::condition_variable mCV;
    std::atomic<bool>       mStarted {};
    bool                    mConnected {};

    common::utils::Channel<std::vector<uint8_t>> mOutgoingMsgChannel;
    common::utils::Channel<std::vector<uint8_t>> mIncomingMsgChannel;

    std::queue<iamanager::v6::IAMOutgoingMessages> mMessageCache;
};

} // namespace aos::mp::iamclient

#endif
