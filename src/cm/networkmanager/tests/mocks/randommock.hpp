/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_TESTS_MOCKS_RANDOMMOCK_HPP_
#define AOS_CM_NETWORKMANAGER_TESTS_MOCKS_RANDOMMOCK_HPP_

#include <gmock/gmock.h>

#include <core/common/crypto/itf/crypto.hpp>

namespace aos::crypto {

class MockRandom : public RandomItf {
public:
    MOCK_METHOD(RetWithError<uint64_t>, RandInt, (uint64_t max), (override));
    MOCK_METHOD(Error, RandBuffer, (Array<uint8_t> & buffer, size_t size), (override));
};

} // namespace aos::crypto

#endif
