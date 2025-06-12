/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TESTTOOLS_PARTITION_HPP
#define TESTTOOLS_PARTITION_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <aos/common/tools/error.hpp>

namespace aos::common::testtools {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/**
 * Partition description.
 */
struct PartDesc {
    std::string mType;
    std::string mLabel;
    uint64_t    mSize;
};

/**
 * Partition information.
 */
struct PartInfo : PartDesc {
    std::string mDevice;
    std::string mPartUUID;
};

/**
 * Test disk.
 */
struct TestDisk {
    std::string           mDevice;
    std::vector<PartInfo> mPartitions;
    std::string           mPath;

    /**
     * Default constructor.
     */
    TestDisk() = default;

    /**
     * Constructor.
     *
     * @param p Path to the disk.
     */
    explicit TestDisk(const std::string& p)
        : mPath(p)
    {
    }

    /**
     * Close the disk.
     *
     * @return Error.
     */
    Error Close();
};

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

/**
 * Create a new test disk.
 *
 * @param path Path to the disk.
 * @param desc Partition descriptions.
 * @return Test disk.
 */
RetWithError<TestDisk> NewTestDisk(const std::string& path, const std::vector<PartDesc>& desc);

} // namespace aos::common::testtools

#endif // TESTTOOLS_PARTITION_HPP
