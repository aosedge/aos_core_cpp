/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_TESTS_MOCKS_CONTAINERRUNNERMOCK_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_TESTS_MOCKS_CONTAINERRUNNERMOCK_HPP_

#include <gmock/gmock.h>

#include <sm/launcher/runtimes/container/itf/containerrunner.hpp>

namespace aos::sm::launcher {

class ContainerRunnerMock : public ContainerRunnerItf {
public:
    MOCK_METHOD(Error, StartContainer, (const std::string&), (override));
    MOCK_METHOD(RetWithError<ContainerStatus>, GetContainerStatus, (const std::string&), (override));
    MOCK_METHOD((RetWithError<std::vector<ContainerStatus>>), ListContainers, (), (override));
    MOCK_METHOD(Error, StopContainer, (const std::string&), (override));
    MOCK_METHOD(Error, RemoveContainer, (const std::string&), (override));
};

} // namespace aos::sm::launcher

#endif
