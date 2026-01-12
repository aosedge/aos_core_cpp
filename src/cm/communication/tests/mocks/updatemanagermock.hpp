/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_MOCKS_UPDATEMANAGERMOCK_HPP_
#define AOS_CM_COMMUNICATION_TESTS_MOCKS_UPDATEMANAGERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/cm/updatemanager/itf/updatemanager.hpp>

namespace aos::cm::updatemanager {

/**
 * Update manager mock.
 */
class UpdateManagerMock : public UpdateManagerItf {
public:
    MOCK_METHOD(Error, ProcessDesiredStatus, (const DesiredStatus&), (override));
};

} // namespace aos::cm::updatemanager

#endif
