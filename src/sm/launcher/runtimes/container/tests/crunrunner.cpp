/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/stat.h>

#include <chrono>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <common/utils/filesystem.hpp>

#include <sm/launcher/runtimes/container/crunrunner.hpp>

using namespace testing;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

// Writes a stub "crun" executable that ignores all arguments and exits with the given code, so
// CRunRunner's process tracking (ExecAsyncCommand + waitpid reaping) can be exercised without a real
// crun/container stack.
std::string CreateCRunStub(const std::string& dir, int exitCode)
{
    const auto path = dir + "/crun_stub.sh";

    std::ofstream file(path);
    file << "#!/bin/sh\nexit " << exitCode << "\n";
    file.close();

    ::chmod(path.c_str(), 0700);

    return path;
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return predicate();
}

} // namespace

class CRunRunnerTest : public Test {
public:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        auto [dir, err] = common::utils::MkTmpDir();
        ASSERT_TRUE(err.IsNone());

        mTmpDir = dir;
    }

protected:
    std::string mTmpDir;
    CRunRunner  mRunner;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CRunRunnerTest, StartContainerReportsExitCodeOnceProcessExits)
{
    const auto crunStub = CreateCRunStub(mTmpDir, 3);

    ASSERT_TRUE(mRunner.Init(mTmpDir, mTmpDir, crunStub).IsNone());
    ASSERT_TRUE(mRunner.StartContainer("instance0").IsNone());

    RetWithError<ContainerStatus> result = {ContainerStatus {}, ErrorEnum::eNone};

    const auto reaped = WaitUntil([&] {
        result = mRunner.GetContainerStatus("instance0");

        return result.mValue.mExitCode.HasValue();
    });

    ASSERT_TRUE(reaped);
    ASSERT_TRUE(result.mError.IsNone());
    ASSERT_TRUE(result.mValue.mExitCode.HasValue());
    EXPECT_EQ(result.mValue.mExitCode.GetValue(), 3);
}

TEST_F(CRunRunnerTest, StartContainerReportsZeroExitCode)
{
    const auto crunStub = CreateCRunStub(mTmpDir, 0);

    ASSERT_TRUE(mRunner.Init(mTmpDir, mTmpDir, crunStub).IsNone());
    ASSERT_TRUE(mRunner.StartContainer("instance0").IsNone());

    RetWithError<ContainerStatus> result = {ContainerStatus {}, ErrorEnum::eNone};

    const auto reaped = WaitUntil([&] {
        result = mRunner.GetContainerStatus("instance0");

        return result.mValue.mExitCode.HasValue();
    });

    ASSERT_TRUE(reaped);
    ASSERT_TRUE(result.mValue.mExitCode.HasValue());
    EXPECT_EQ(result.mValue.mExitCode.GetValue(), 0);
}

TEST_F(CRunRunnerTest, ListContainersReportsReapedExitCode)
{
    const auto crunStub = CreateCRunStub(mTmpDir, 5);

    ASSERT_TRUE(mRunner.Init(mTmpDir, mTmpDir, crunStub).IsNone());
    ASSERT_TRUE(mRunner.StartContainer("instance0").IsNone());

    std::vector<ContainerStatus> statuses;

    const auto reaped = WaitUntil([&] {
        auto [result, err] = mRunner.ListContainers();
        statuses           = result;

        return !statuses.empty() && statuses[0].mExitCode.HasValue();
    });

    ASSERT_TRUE(reaped);
    ASSERT_EQ(statuses.size(), 1U);
    ASSERT_TRUE(statuses[0].mExitCode.HasValue());
    EXPECT_EQ(statuses[0].mExitCode.GetValue(), 5);
}

TEST_F(CRunRunnerTest, RemoveContainerClearsTrackedExitCode)
{
    const auto crunStub = CreateCRunStub(mTmpDir, 4);

    ASSERT_TRUE(mRunner.Init(mTmpDir, mTmpDir, crunStub).IsNone());
    ASSERT_TRUE(mRunner.StartContainer("instance0").IsNone());

    ASSERT_TRUE(
        WaitUntil([&] { return mRunner.GetContainerStatus("instance0").mValue.mExitCode.HasValue(); }));

    // RemoveContainer also tries to tear down real crun state, which does not exist here, so its
    // returned Error is not meaningful in this test; only the local pid/exit-code bookkeeping is checked.
    mRunner.RemoveContainer("instance0");

    const auto status = mRunner.GetContainerStatus("instance0");

    EXPECT_FALSE(status.mValue.mExitCode.HasValue());
}

TEST_F(CRunRunnerTest, StartContainerFailsForInvalidExecutable)
{
    ASSERT_TRUE(mRunner.Init(mTmpDir, mTmpDir, mTmpDir + "/no-such-crun").IsNone());

    EXPECT_FALSE(mRunner.StartContainer("instance0").IsNone());

    auto [statuses, err] = mRunner.ListContainers();

    EXPECT_TRUE(statuses.empty());
}

} // namespace aos::sm::launcher
