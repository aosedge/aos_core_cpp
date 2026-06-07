/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_TESTS_MOCKS_PROCESSSPAWNERMOCK_HPP_
#define AOS_COMMON_TESTS_MOCKS_PROCESSSPAWNERMOCK_HPP_

#include <gmock/gmock.h>

#include <common/process/itf/processspawner.hpp>

namespace aos::common::process {

class MockProcessSpawner : public ProcessSpawnerItf {
public:
    MOCK_METHOD(
        (RetWithError<Poco::Process::PID>), Spawn, (const std::string&, const std::vector<std::string>&), (override));
    MOCK_METHOD(Error, Kill, (Poco::Process::PID), (override));
    MOCK_METHOD(Error, Signal, (Poco::Process::PID, int), (override));
    MOCK_METHOD(bool, IsAlive, (Poco::Process::PID), (const, override));
    MOCK_METHOD((RetWithError<std::string>), GetCmdLine, (Poco::Process::PID), (const, override));
};

} // namespace aos::common::process

#endif
