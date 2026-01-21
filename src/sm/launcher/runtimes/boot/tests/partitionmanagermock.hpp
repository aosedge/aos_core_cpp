/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_TESTS_PARTITIONMANAGERMOCK_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_TESTS_PARTITIONMANAGERMOCK_HPP_

#include <sm/launcher/runtimes/boot/itf/partitionmanager.hpp>

namespace aos::sm::launcher {

/**
 * Partition manager interface.
 */
class PartitionManagerMock : public PartitionManagerItf {
public:
    MOCK_METHOD(Error, GetPartInfo, (const std::string&, PartInfo&), (const, override));

    MOCK_METHOD(Error, Mount, (const PartInfo&, const std::string&, int), (const, override));

    MOCK_METHOD(Error, Unmount, (const std::string&), (const, override));

    MOCK_METHOD(Error, CopyDevice, (const std::string&, const std::string&), (const, override));

    MOCK_METHOD(Error, InstallImage, (const std::string&, const std::string&), (const, override));
};

} // namespace aos::sm::launcher

#endif
