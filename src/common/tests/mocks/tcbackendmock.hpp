/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_TESTS_MOCKS_TCBACKENDMOCK_HPP_
#define AOS_COMMON_TESTS_MOCKS_TCBACKENDMOCK_HPP_

#include <gmock/gmock.h>

#include <common/network/itf/tcbackend.hpp>

namespace aos::common::network {

class MockTCBackend : public TCBackendItf {
public:
    MOCK_METHOD(Error, AddRootTBFQDisc, (const String&, const TBFParams&), (override));
    MOCK_METHOD(Error, DelRootTBFQDisc, (const String&), (override));
    MOCK_METHOD(Error, AddIngressQDisc, (const String&), (override));
    MOCK_METHOD(Error, DelIngressQDisc, (const String&), (override));
    MOCK_METHOD(Error, AddIngressMirredFilter, (const String&, const String&), (override));
};

} // namespace aos::common::network

#endif
