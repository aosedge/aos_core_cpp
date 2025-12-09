/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_TESTS_MOCKS_SYSTEMDCONNMOCK_HPP_
#define AOS_SM_TESTS_MOCKS_SYSTEMDCONNMOCK_HPP_

#include <gmock/gmock.h>

#include <sm/utils/systemdconn.hpp>

namespace aos::sm::utils {

class SystemdConnMock : public SystemdConnItf {
public:
    MOCK_METHOD(RetWithError<std::vector<UnitStatus>>, ListUnits, (), (override));

    MOCK_METHOD(RetWithError<UnitStatus>, GetUnitStatus, (const std::string& name), (override));
    MOCK_METHOD(
        Error, StartUnit, (const std::string& name, const std::string& mode, const Duration& timeout), (override));
    MOCK_METHOD(
        Error, StopUnit, (const std::string& name, const std::string& mode, const Duration& timeout), (override));

    MOCK_METHOD(Error, ResetFailedUnit, (const std::string& name), (override));
};

} // namespace aos::sm::utils

#endif
