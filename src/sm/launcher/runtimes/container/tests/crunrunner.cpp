/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/stat.h>

#include <chrono>
#include <filesystem>
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

// Writes a stub "crun" executable that exits with the given code, so CRunRunner's process tracking
// (ExecAsyncCommand + waitpid reaping) can be exercised without a real crun/container stack. It is invoked as
// `crun_stub.sh --root <stateRoot> run -b <bundleDir> <instanceID>`; before exiting it creates
// "<stateRoot>/<instanceID>/status", the marker real crun leaves behind that CRunRunner::ListContainers() looks
// for when scanning the state root directory.
std::string CreateCRunStub(const std::string& dir, int exitCode)
{
    const auto path = dir + "/crun_stub.sh";

    std::ofstream file(path);
    file << "#!/bin/sh\nmkdir -p \"$2/$6\"\ntouch \"$2/$6/status\"\nexit " << exitCode << "\n";
    file.close();

    ::chmod(path.c_str(), 0700);

    return path;
}

// Like CreateCRunStub, but sleeps before creating its state marker and exiting - simulating a real crun, which
// spends several ms on namespace/cgroup/mount setup before it ever writes its on-disk state. Used to reproduce
// the race where a status query lands in that window: real crun (and this stub) is still starting up, so its
// own state doesn't exist on disk yet, but the process is still alive and tracked locally.
std::string CreateSlowCRunStub(const std::string& dir, int exitCode, std::chrono::milliseconds delay)
{
    const auto path = dir + "/crun_slow_stub.sh";

    std::ofstream file(path);
    file << "#!/bin/sh\nsleep " << (delay.count() / 1000.0) << "\nmkdir -p \"$2/$6\"\ntouch \"$2/$6/status\"\nexit "
         << exitCode << "\n";
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

        // libcrun_get_containers_list() (used by ListContainers()) fails outright if the state root holds
        // anything crun itself did not create, so it must be its own directory - not shared with the stub
        // executable or bundle dir, the way a real deployment keeps runtimeDir and CRunStateRoot apart.
        mStateRoot = mTmpDir + "/state";
        std::filesystem::create_directories(mStateRoot);
    }

protected:
    std::string mTmpDir;
    std::string mStateRoot;
    CRunRunner  mRunner;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CRunRunnerTest, StartContainerReportsExitCodeOnceProcessExits)
{
    const auto crunStub = CreateCRunStub(mTmpDir, 3);

    ASSERT_TRUE(mRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());
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

    ASSERT_TRUE(mRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());
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

    ASSERT_TRUE(mRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());
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

    ASSERT_TRUE(mRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());
    ASSERT_TRUE(mRunner.StartContainer("instance0").IsNone());

    ASSERT_TRUE(WaitUntil([&] { return mRunner.GetContainerStatus("instance0").mValue.mExitCode.HasValue(); }));

    // RemoveContainer also tries to tear down crun state via libcrun, but the stub only leaves behind a
    // placeholder status file (not a real one), so its returned Error is not meaningful in this test; only the
    // local pid/exit-code bookkeeping is checked.
    mRunner.RemoveContainer("instance0");

    const auto status = mRunner.GetContainerStatus("instance0");

    EXPECT_FALSE(status.mValue.mExitCode.HasValue());
}

TEST_F(CRunRunnerTest, PersistedExitCodeSurvivesRunnerRestart)
{
    const auto crunStub = CreateCRunStub(mTmpDir, 7);

    // Simulate the OCI bundle dir the launcher's Instance class prepares before StartContainer() is ever
    // called - CRunRunner persists the exit code alongside it, under mRuntimeDir/<instanceID>.
    std::filesystem::create_directories(mTmpDir + "/instance0");

    ASSERT_TRUE(mRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());
    ASSERT_TRUE(mRunner.StartContainer("instance0").IsNone());

    ASSERT_TRUE(WaitUntil([&] { return mRunner.GetContainerStatus("instance0").mValue.mExitCode.HasValue(); }));

    // A fresh CRunRunner, as constructed after an SM process restart, has no in-memory record of the pid or
    // exit code - it must recover the exit code from disk instead.
    CRunRunner freshRunner;
    ASSERT_TRUE(freshRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());

    auto status = freshRunner.GetContainerStatus("instance0");

    ASSERT_TRUE(status.mError.IsNone());
    ASSERT_TRUE(status.mValue.mExitCode.HasValue());
    EXPECT_EQ(status.mValue.mExitCode.GetValue(), 7);
    EXPECT_EQ(status.mValue.mState, InstanceStateEnum::eFailed);
}

TEST_F(CRunRunnerTest, RemoveContainerClearsPersistedExitCode)
{
    const auto crunStub = CreateCRunStub(mTmpDir, 4);

    std::filesystem::create_directories(mTmpDir + "/instance0");

    ASSERT_TRUE(mRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());
    ASSERT_TRUE(mRunner.StartContainer("instance0").IsNone());
    ASSERT_TRUE(WaitUntil([&] { return mRunner.GetContainerStatus("instance0").mValue.mExitCode.HasValue(); }));

    mRunner.RemoveContainer("instance0");

    // A restart (RemoveContainer followed by StartContainer for the same instance ID, as
    // Runner::RestartInstances() does) must not resurface the previous run's exit code via a fresh runner.
    CRunRunner freshRunner;
    ASSERT_TRUE(freshRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());

    auto status = freshRunner.GetContainerStatus("instance0");

    EXPECT_FALSE(status.mValue.mExitCode.HasValue());
}

TEST_F(CRunRunnerTest, GetContainerStatusReportsActiveBeforeCRunWritesItsOwnState)
{
    // Real crun spends several ms on namespace/cgroup/mount setup before it writes its own on-disk state, but
    // StartContainer() only waits for fork() - it returns long before that setup finishes. A status query
    // landing in that window must not mistake "crun hasn't written state yet" for "the container failed": we
    // still hold a live, unreaped pid for it, which is what matters.
    const auto crunStub = CreateSlowCRunStub(mTmpDir, 0, std::chrono::milliseconds(300));

    ASSERT_TRUE(mRunner.Init(mTmpDir, mStateRoot, crunStub).IsNone());
    ASSERT_TRUE(mRunner.StartContainer("instance0").IsNone());

    auto status = mRunner.GetContainerStatus("instance0");

    ASSERT_TRUE(status.mError.IsNone());
    EXPECT_EQ(status.mValue.mState, InstanceStateEnum::eActive);
    EXPECT_FALSE(status.mValue.mExitCode.HasValue());
}

TEST_F(CRunRunnerTest, StartContainerFailsForInvalidExecutable)
{
    ASSERT_TRUE(mRunner.Init(mTmpDir, mStateRoot, mTmpDir + "/no-such-crun").IsNone());

    EXPECT_FALSE(mRunner.StartContainer("instance0").IsNone());

    auto [statuses, err] = mRunner.ListContainers();

    EXPECT_TRUE(statuses.empty());
}

} // namespace aos::sm::launcher
