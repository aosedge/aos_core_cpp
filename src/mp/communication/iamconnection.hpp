/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_COMMUNICATION_IAMCONNECTION_HPP_
#define AOS_MP_COMMUNICATION_IAMCONNECTION_HPP_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <aos/common/tools/error.hpp>

#include <common/iamclient/publicservicehandler.hpp>

#include "types.hpp"

namespace aos::mp::communication {

/**
 * IAM connection class.
 */
class IAMConnection {
public:
    /**
     * Initializes connection.
     *
     * @param port Port.
     * @param certProvider Certificate provider.
     * @param comManager Communication manager.
     * @param channel Channel.
     * @return Error.
     */
    Error Init(int port, HandlerItf& handler, CommunicationManagerItf& comManager,
        common::iamclient::TLSCredentialsItf* certProvider = nullptr, const std::string& certStorage = "");

    /**
     * Starts the connection.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops the connection.
     *
     * @return Error.
     */
    Error Stop();

private:
    static constexpr auto cConnectionTimeout = std::chrono::seconds(3);

    void  Run();
    Error ReadHandler();
    void  WriteHandler();

    std::atomic<bool>               mShutdown {};
    std::thread                     mConnectThread;
    std::shared_ptr<CommChannelItf> mIAMCommChannel;
    HandlerItf*                     mHandler {};

    std::mutex              mMutex;
    std::condition_variable mCondVar;
};

} // namespace aos::mp::communication

#endif
