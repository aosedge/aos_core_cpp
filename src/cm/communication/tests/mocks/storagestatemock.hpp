/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_MOCKS_STORAGESTATEMOCK_HPP_
#define AOS_CM_COMMUNICATION_TESTS_MOCKS_STORAGESTATEMOCK_HPP_

#include <gmock/gmock.h>

#include <core/cm/storagestate/itf/storagestate.hpp>

namespace aos::cm::storagestate {

/**
 * State handler mock.
 */
class StateHandlerMock : public StateHandlerItf {
public:
    MOCK_METHOD(Error, UpdateState, (const aos::UpdateState&), (override));
    MOCK_METHOD(Error, AcceptState, (const StateAcceptance&), (override));
};

} // namespace aos::cm::storagestate

#endif
