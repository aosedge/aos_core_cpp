/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "testtools/partition.hpp"
#include "utils/fsplatform.hpp"
#include "utils/utils.hpp"

namespace aos::common::utils::test {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class DISABLED_FSPlatformTest : public ::testing::Test {
    void SetUp() override
    {
        char tmpDirTemplate[] = "/tmp/um_XXXXXX";
        ASSERT_TRUE(mkdtemp(tmpDirTemplate) != nullptr) << "Error creating tmp dir";

        mTmpDir             = tmpDirTemplate;
        mPlatformMountPoint = mTmpDir / "platform";
        mTestDir            = mPlatformMountPoint / "testdir";

        std::filesystem::create_directory(mPlatformMountPoint);

        std::vector<testtools::PartDesc> partitions = {{"vfat", "efi", 16}, {"ext4", "platform", 32}};

        auto [disk, err] = testtools::NewTestDisk(mTmpDir / "testdisk.img", partitions);
        ASSERT_TRUE(err.IsNone()) << "Failed to create test disk: " << err.Message();
        mDisk = disk;

        auto res = utils::ExecCommand({"mount", mDisk.mPartitions[1].mDevice, mPlatformMountPoint});
        ASSERT_TRUE(res.mError.IsNone()) << "Failed to mount platform partition: " << res.mError.Message();

        std::filesystem::create_directory(mTestDir);

        res = utils::ExecCommand(
            {"dd", "if=/dev/urandom", "of=" + (mTestDir / "largefile").string(), "bs=1M", "count=3"});
        ASSERT_TRUE(res.mError.IsNone()) << "Failed to create 3MB test file: " << res.mError.Message();

        std::ofstream smallFile(mTestDir / "smallfile.txt");
        smallFile << "This is a small text file for testing" << std::endl;
        smallFile.close();
    }

    void TearDown() override
    {
        auto res = utils::ExecCommand({"umount", mPlatformMountPoint});
        ASSERT_TRUE(res.mError.IsNone()) << "Error unmounting platform partition: " << res.mError.Message();

        if (!mDisk.mDevice.empty()) {
            Error err = mDisk.Close();
            ASSERT_TRUE(err.IsNone()) << "Error closing test disk: " << err.Message();
        }

        if (!mTmpDir.empty() && std::filesystem::exists(mTmpDir)) {
            res = utils::ExecCommand({"rm", "-rf", mTmpDir});
            ASSERT_TRUE(res.mError.IsNone()) << "Error removing tmp dir: " << res.mError.Message();
        }
    }

protected:
    std::filesystem::path mTmpDir;
    std::filesystem::path mPlatformMountPoint;
    testtools::TestDisk   mDisk;
    std::filesystem::path mTestDir;
    FSPlatform            mFsplatform;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(DISABLED_FSPlatformTest, DiskCreatedSuccessfully)
{
    ASSERT_FALSE(mDisk.mDevice.empty());
    ASSERT_EQ(mDisk.mPartitions.size(), 2);
    ASSERT_EQ(mDisk.mPartitions[0].mType, "vfat");
    ASSERT_EQ(mDisk.mPartitions[1].mType, "ext4");
}

TEST_F(DISABLED_FSPlatformTest, GetMountPoint)
{
    auto [mountPoint, err] = mFsplatform.GetMountPoint(String(mTestDir.c_str()));
    ASSERT_TRUE(err.IsNone()) << "Failed to get mount point: " << err.Message();
    ASSERT_TRUE(!mountPoint.IsEmpty()) << "Mount point is not empty";
    auto pos = mTestDir.string().find(mountPoint.CStr());
    ASSERT_NE(pos, std::string::npos) << "Mount point is not a prefix of test dir";
}

TEST_F(DISABLED_FSPlatformTest, GetTotalSize)
{
    auto [result, err] = mFsplatform.GetTotalSize(String(mPlatformMountPoint.c_str()));
    ASSERT_TRUE(err.IsNone()) << "Failed to get total size: " << err.Message();

    // The total size should be approximately 32 MiB (minus filesystem overhead)
    // 32 MiB = 33554432 bytes
    // For ext4, substantial space is used for filesystem metadata (journal, inodes, etc.)
    // From test results, we see the actual size is around 25MB
    const int64_t minExpected = 25 * 1024 * 1024; // 25 MB
    const int64_t maxExpected = 30 * 1024 * 1024; // 30 MB

    ASSERT_GE(result, minExpected) << "Total size too small: " << result << " bytes, expected at least " << minExpected
                                   << " bytes";
    ASSERT_LE(result, maxExpected) << "Total size too large: " << result << " bytes, expected at most " << maxExpected
                                   << " bytes";
}

TEST_F(DISABLED_FSPlatformTest, GetDirSize)
{
    auto [dirSize, err] = mFsplatform.GetDirSize(String(mTestDir.c_str()));
    ASSERT_TRUE(err.IsNone()) << "Failed to get directory size: " << err.Message();

    const int64_t expectedMinSize = 3 * 1024 * 1024;

    ASSERT_GE(dirSize, expectedMinSize) << "Directory size too small: " << dirSize << " bytes, expected at least "
                                        << expectedMinSize << " bytes";
    ASSERT_LE(dirSize, expectedMinSize + 1024 * 1024) << "Directory size too large: " << dirSize << " bytes";
}

TEST_F(DISABLED_FSPlatformTest, GetAvailableSize)
{
    auto [availSize, err] = mFsplatform.GetAvailableSize(String(mPlatformMountPoint.c_str()));
    ASSERT_TRUE(err.IsNone()) << "Failed to get available size: " << err.Message();

    auto [totalSize, totalErr] = mFsplatform.GetTotalSize(String(mPlatformMountPoint.c_str()));
    ASSERT_TRUE(totalErr.IsNone()) << "Failed to get total size: " << totalErr.Message();

    ASSERT_GT(availSize, 0) << "Available size should be greater than 0";
    ASSERT_LT(availSize, totalSize) << "Available size should be less than total size";

    const int64_t usedSpace = totalSize - availSize;

    const int64_t expectedMinUsed = 4 * 1024 * 1024;
    const int64_t expectedMaxUsed = 7 * 1024 * 1024;

    ASSERT_GE(usedSpace, expectedMinUsed)
        << "Used space too small: " << usedSpace << " bytes, expected at least " << expectedMinUsed << " bytes";
    ASSERT_LE(usedSpace, expectedMaxUsed)
        << "Used space too large: " << usedSpace << " bytes, expected at most " << expectedMaxUsed << " bytes";
}

TEST_F(DISABLED_FSPlatformTest, GetMountPointInvalidPath)
{
    auto [result, err] = mFsplatform.GetMountPoint(String("/nonexistent/path"));
    ASSERT_TRUE(!err.IsNone()) << "Should fail for nonexistent path";
}

} // namespace aos::common::utils::test
