/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_STUBS_MESSAGEHANDLER_HPP_
#define AOS_CM_COMMUNICATION_TESTS_STUBS_MESSAGEHANDLER_HPP_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <core/cm/communication/communication.hpp>

namespace aos::cm::communication {

/**
 * Message handler stub.
 */
class MessageHandlerStub : public MessageHandlerItf {
public:
    /**
     * Handles received message.
     *
     * @param message received message.
     * @return Error.
     */
    Error HandleMessage(const aos::cloudprotocol::MessageVariant& message) override
    {
        std::lock_guard lock {mMutex};

        mMessages.push_back(message);
        mCondVar.notify_all();

        return ErrorEnum::eNone;
    }

    Error WaitMessageReceived(
        const aos::cloudprotocol::MessageVariant& message, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};

        if (!mCondVar.wait_for(lock, timeout, [this, &message] {
                return std::find_if(mMessages.begin(), mMessages.end(), [&message](const auto& msg) {
                    return message == msg;
                }) != mMessages.end();
            })) {
            return ErrorEnum::eTimeout;
        }

        return ErrorEnum::eNone;
    }

private:
    std::vector<aos::cloudprotocol::MessageVariant> mMessages;
    std::mutex                                      mMutex;
    std::condition_variable                         mCondVar;
};

} // namespace aos::cm::communication

#endif
