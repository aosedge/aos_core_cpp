/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_STUBS_CONNECTION_SUBSCRIBER_HPP_
#define AOS_CM_COMMUNICATION_TESTS_STUBS_CONNECTION_SUBSCRIBER_HPP_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <core/common/connectionprovider/connectionprovider.hpp>

namespace aos {

/**
 * Connection subscriber stub.
 */
class ConnectionSubscriberStub : public ConnectionSubscriberItf {
public:
    void OnConnect() override
    {
        std::lock_guard lock {mMutex};

        mConnected = true;
        mCondVar.notify_all();
    }

    void OnDisconnect() override
    {
        std::lock_guard lock {mMutex};

        mConnected = false;
        mCondVar.notify_all();
    }

    Error WaitEvent(bool connected, std::chrono::milliseconds timeout = std::chrono::seconds(10))
    {
        std::unique_lock lock {mMutex};

        if (!mCondVar.wait_for(
                lock, timeout, [this, connected] { return mConnected.has_value() && *mConnected == connected; })) {
            return ErrorEnum::eTimeout;
        }

        mConnected.reset();

        return ErrorEnum::eNone;
    }

private:
    std::mutex              mMutex;
    std::condition_variable mCondVar;
    std::optional<bool>     mConnected;
};

} // namespace aos

#endif
