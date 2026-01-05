/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_TESTS_MOCKS_FILESYSTEMMOCK_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_TESTS_MOCKS_FILESYSTEMMOCK_HPP_

#include <gmock/gmock.h>

#include <sm/launcher/runtimes/container/itf/filesystem.hpp>

namespace aos::sm::launcher {

/**
 * File system mock.
 */
class FileSystemMock : public FileSystemItf {
public:
    MOCK_METHOD(Error, CreateHostFSWhiteouts, (const std::string&, const std::vector<std::string>&), (override));
    MOCK_METHOD(Error, CreateMountPoints, (const std::string&, const std::vector<Mount>&), (override));
    MOCK_METHOD(Error, MountServiceRootFS, (const std::string&, const std::vector<std::string>&), (override));
    MOCK_METHOD(Error, UmountServiceRootFS, (const std::string&), (override));
    MOCK_METHOD(Error, PrepareServiceStorage, (const std::string&, uid_t, gid_t), (override));
    MOCK_METHOD(Error, PrepareServiceState, (const std::string&, uid_t, gid_t), (override));
    MOCK_METHOD(Error, PrepareNetworkDir, (const std::string&), (override));
    MOCK_METHOD(RetWithError<std::string>, GetAbsPath, (const std::string&), (override));
    MOCK_METHOD(RetWithError<gid_t>, GetGIDByName, (const std::string&), (override));
    MOCK_METHOD(Error, PopulateHostDevices, (const std::string&, std::vector<oci::LinuxDevice>&), (override));
    MOCK_METHOD(Error, ClearDir, (const std::string&), (override));
    MOCK_METHOD(Error, RemoveAll, (const std::string&), (override));
    MOCK_METHOD(RetWithError<std::vector<std::string>>, ListDir, (const std::string&), (override));
};

}; // namespace aos::sm::launcher

#endif
