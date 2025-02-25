/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMCONNECTION_HPP_
#define IAMCONNECTION_HPP_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <aos/common/tools/error.hpp>

#include "types.hpp"
#include <iamclient/publicservicehandler.hpp>

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
     * Closes connection.
     *
     */
    void Close();

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

#endif /* IAMCONNECTION_HPP_ */
