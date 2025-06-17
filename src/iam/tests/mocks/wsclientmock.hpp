/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_TESTS_MOCKS_WSCLIENTMOCK_HPP_
#define AOS_TESTS_MOCKS_WSCLIENTMOCK_HPP_

#include <memory>

#include <gmock/gmock.h>

#include <iam/visidentifier/wsclient.hpp>

namespace aos::iam::visidentifier {

/**
 * Subjects observer mock.
 */
class WSClientMock : public WSClientItf {
public:
    MOCK_METHOD(void, Connect, (), (override));
    MOCK_METHOD(void, Close, (), (override));
    MOCK_METHOD(void, Disconnect, (), (override));
    MOCK_METHOD(std::string, GenerateRequestID, (), (override));
    MOCK_METHOD(WSClientEvent::Details, WaitForEvent, (), (override));
    MOCK_METHOD(ByteArray, SendRequest, (const std::string&, const ByteArray&), (override));
    MOCK_METHOD(void, AsyncSendMessage, (const ByteArray&), (override));
};

using WSClientMockPtr = std::shared_ptr<WSClientMock>;

} // namespace aos::iam::visidentifier

#endif
