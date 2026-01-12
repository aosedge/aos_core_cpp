/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_MOCKS_LAUNCHERMOCK_HPP_
#define AOS_CM_COMMUNICATION_TESTS_MOCKS_LAUNCHERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/cm/launcher/itf/envvarhandler.hpp>

namespace aos::cm::launcher {

/**
 * Env var handler mock.
 */
class EnvVarHandlerMock : public EnvVarHandlerItf {
public:
    MOCK_METHOD(Error, OverrideEnvVars, (const OverrideEnvVarsRequest& envVars), (override));
};

} // namespace aos::cm::launcher

#endif
