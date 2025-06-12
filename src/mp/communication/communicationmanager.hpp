/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_COMMUNICATION_COMMUNICATIONMANAGER_HPP_
#define AOS_MP_COMMUNICATION_COMMUNICATIONMANAGER_HPP_

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "mp/config/config.hpp"

#include "cmconnection.hpp"
#include "communicationchannel.hpp"
#include "iamconnection.hpp"
#include "types.hpp"

namespace aos::mp::communication {

/**
 * Communication manager class.
 */
class CommunicationManager : public CommunicationManagerItf, public iam::certhandler::CertReceiverItf {
public:
    /**
     * Constructor.
     */
    CommunicationManager() = default;

    /**
     * Initializes communication manager.
     *
     * @param cfg Configuration
     * @param transport Transport
     * @param iamChannel IAM channel
     * @param cmChannel CM channel
     * @param certProvider Certificate provider
     * @param certLoader Certificate loader
     * @param cryptoProvider Crypto provider
     * @return Error
     */
    Error Init(const config::Config& cfg, TransportItf& transport, crypto::CertLoaderItf* certLoader = nullptr,
        crypto::x509::ProviderItf* cryptoProvider = nullptr);

    /**
     * Starts the communication manager.
     *
     * @return Error
     */
    Error Start();

    /**
     * Stops the communication manager.
     *
     * @return Error
     */
    Error Stop();

    /**
     * Creates communication channel.
     *
     * @param port Port
     * @param certProvider Certificate provider
     * @return std::unique_ptr<CommChannelItf>
     */
    std::shared_ptr<CommChannelItf> CreateChannel(
        int port, common::iamclient::TLSCredentialsItf* certProvider, const std::string& certStorage) override;

    /**
     * Connects to the communication manager.
     *
     * @return Error
     */
    Error Connect() override;

    /**
     * Reads message from the communication manager.
     *
     * @param message Message
     * @return Error
     */
    Error Read(std::vector<uint8_t>& message) override;

    /**
     * Writes message to the communication manager.
     *
     * @param message Message
     * @return Error
     */
    Error Write(std::vector<uint8_t> message) override;

    /**
     * Closes the communication manager.
     *
     * @return Error
     */
    Error Close() override;

    /**
     * Checks if the communication manager is connected.
     *
     * @return bool
     */
    bool IsConnected() const override;

    /**
     * Subscribes to certificate changes.
     *
     * @param certType Certificate type.
     */
    void OnCertChanged(const iam::certhandler::CertInfo& info) override;

private:
    static constexpr auto                      cMaxMessageSize    = 64 * 1024; // 64 KB
    static constexpr std::chrono::milliseconds cConnectionTimeout = std::chrono::seconds(5);
    static constexpr std::chrono::seconds      cReconnectTimeout  = std::chrono::seconds(3);

    void  Run();
    Error ReadHandler();

    TransportItf*                                        mTransport {};
    crypto::CertLoaderItf*                               mCertLoader {};
    crypto::x509::ProviderItf*                           mCryptoProvider {};
    const config::Config*                                mCfg {};
    std::map<int, std::shared_ptr<CommunicationChannel>> mChannels;
    std::thread                                          mThread;
    std::atomic<bool>                                    mShutdown {};
    std::atomic<bool>                                    mIsConnected {};
    std::mutex                                           mMutex;
    std::condition_variable                              mCondVar;
};

} // namespace aos::mp::communication

#endif
