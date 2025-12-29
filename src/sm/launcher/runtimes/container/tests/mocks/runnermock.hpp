/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_TESTS_MOCKS_RUNSTATUSRECEIVERMOCK_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_TESTS_MOCKS_RUNSTATUSRECEIVERMOCK_HPP_

#include <gmock/gmock.h>

#include <sm/launcher/runtimes/container/itf/runner.hpp>

namespace aos::sm::launcher {

/**
 * Run status receiver mock.
 */
class RunStatusReceiverMock : public RunStatusReceiverItf {
public:
    MOCK_METHOD(Error, UpdateRunStatus, (const std::vector<RunStatus>&), (override));
};

/**
 * Runner mock.
 */
class RunnerMock : public RunnerItf {
public:
    MOCK_METHOD(Error, Init, (RunStatusReceiverItf&), (override));
    MOCK_METHOD(Error, Start, (), (override));
    MOCK_METHOD(Error, Stop, (), (override));
    MOCK_METHOD(RunStatus, StartInstance, (const std::string&, const RunParameters&), (override));
    MOCK_METHOD(Error, StopInstance, (const std::string&), (override));
};

} // namespace aos::sm::launcher

#endif
