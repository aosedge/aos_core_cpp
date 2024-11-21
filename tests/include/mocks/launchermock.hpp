/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LAUNCHER_MOCK_HPP_
#define LAUNCHER_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/sm/launcher.hpp>

/**
 * Logs observer mock.
 */
class LauncherMock : public aos::sm::launcher::LauncherItf {
public:
    MOCK_METHOD(aos::Error, RunInstances,
        (const aos::Array<aos::ServiceInfo>&, const aos::Array<aos::LayerInfo>&, const aos::Array<aos::InstanceInfo>&,
            bool),
        (override));

    MOCK_METHOD(aos::Error, OverrideEnvVars,
        (const aos::Array<aos::cloudprotocol::EnvVarsInstanceInfo>&, aos::cloudprotocol::EnvVarsInstanceStatusArray&),
        (override));

    MOCK_METHOD(aos::Error, SetCloudConnection, (bool), (override));
};

#endif
