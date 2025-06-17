/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYSTEMDCONN_MOCK_HPP_
#define SYSTEMDCONN_MOCK_HPP_

#include <gmock/gmock.h>

#include "runner/systemdconn.hpp"

namespace aos::sm::runner {

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

} // namespace aos::sm::runner

#endif
