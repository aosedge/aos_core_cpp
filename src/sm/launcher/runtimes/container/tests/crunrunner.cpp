/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <sstream>

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/launcher/runtimes/container/crunrunner.hpp>

using namespace testing;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const std::filesystem::path cTestDir = std::filesystem::absolute("crunrunner_test");

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void WriteFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());

    std::ofstream file(path);

    file << content;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream     file(path);
    std::stringstream content;

    content << file.rdbuf();

    return content.str();
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CRunRunnerTest : public Test {
public:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        std::filesystem::remove_all(cTestDir);
        std::filesystem::create_directories(cTestDir / "crun");

        mConfig.mRuntimeDir       = cTestDir / "runtime";
        mConfig.mCRunStateRoot    = cTestDir / "crun";
        mConfig.mCRunExecutable   = "/usr/bin/crun";
        mConfig.mConmonExecutable = cTestDir / "conmon";
        mConfig.mConmonDir        = cTestDir / "conmon-state";

        ASSERT_TRUE(mRunner.Init(mConfig).IsNone());
    }

    void TearDown() override { std::filesystem::remove_all(cTestDir); }

protected:
    // Creates a fake conmon: a shell script that records its arguments and runs the given body.
    void CreateConmon(const std::string& body)
    {
        WriteFile(mConfig.mConmonExecutable,
            "#!/bin/sh\necho \"$@\" > " + (cTestDir / "conmon.args").string() + "\n" + body + "\n");

        std::filesystem::permissions(mConfig.mConmonExecutable, std::filesystem::perms::owner_all);
    }

    ContainerConfig mConfig;
    CRunRunner      mRunner;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CRunRunnerTest, StartContainerPassesConmonArgs)
{
    CreateConmon(R"(echo '{"data": -1}' >&"$_OCI_SYNCPIPE")");

    EXPECT_FALSE(mRunner.StartContainer("instance0").IsNone());

    const auto persistDir = mConfig.mConmonDir + "/instance0";
    const auto expected
        = "--api-version --cid instance0 --cuuid instance0 --runtime /usr/bin/crun --runtime-arg=--root="
        + mConfig.mCRunStateRoot + " --bundle " + mConfig.mRuntimeDir + "/instance0 --container-pidfile " + persistDir
        + "/container.pid --conmon-pidfile " + persistDir + "/conmon.pid --persist-dir " + persistDir
        + " --log-path passthrough --syslog\n";

    EXPECT_EQ(ReadFile(cTestDir / "conmon.args"), expected);
}

TEST_F(CRunRunnerTest, StartContainerReportsCreateError)
{
    // Records whether the persist dir exists while conmon runs, so the test can check it's cleaned up afterwards.
    CreateConmon("[ -d \"" + mConfig.mConmonDir + "/instance0\" ] && touch " + (cTestDir / "persist.exists").string()
        + "\n" + R"(echo '{"data": -1, "message": "create failed"}' >&"$_OCI_SYNCPIPE")");

    auto err = mRunner.StartContainer("instance0");

    EXPECT_TRUE(err.Is(ErrorEnum::eFailed));
    EXPECT_STREQ(err.Message(), "create failed");

    EXPECT_TRUE(std::filesystem::exists(cTestDir / "persist.exists"));
    EXPECT_FALSE(std::filesystem::exists(mConfig.mConmonDir + "/instance0"));
}

TEST_F(CRunRunnerTest, StartContainerConmonExitsWithoutResult)
{
    CreateConmon("exit 0");

    auto err = mRunner.StartContainer("instance0");

    EXPECT_TRUE(err.Is(ErrorEnum::eFailed));
    EXPECT_STREQ(err.Message(), "container creation failed, see conmon log for details");
}

TEST_F(CRunRunnerTest, StartContainerConmonFails)
{
    CreateConmon("exit 1");

    auto err = mRunner.StartContainer("instance0");

    EXPECT_TRUE(err.Is(ErrorEnum::eFailed));
    EXPECT_STREQ(err.Message(), "conmon failed: exit=1");
}

TEST_F(CRunRunnerTest, StartContainerConmonNotFound)
{
    EXPECT_FALSE(mRunner.StartContainer("instance0").IsNone());
}

TEST_F(CRunRunnerTest, GetContainerStatusExitCode)
{
    WriteFile(mConfig.mConmonDir + "/instance0/exit", "3");

    auto [status, err] = mRunner.GetContainerStatus("instance0");

    ASSERT_TRUE(err.IsNone());
    EXPECT_EQ(status.mInstanceID, "instance0");
    EXPECT_EQ(status.mState, InstanceStateEnum::eFailed);
    ASSERT_TRUE(status.mExitCode.HasValue());
    EXPECT_EQ(*status.mExitCode, 3);
    EXPECT_FALSE(status.mOOMKilled);
}

TEST_F(CRunRunnerTest, GetContainerStatusOOMKilled)
{
    WriteFile(mConfig.mConmonDir + "/instance0/exit", "137");
    WriteFile(mConfig.mConmonDir + "/instance0/oom", "");

    auto [status, err] = mRunner.GetContainerStatus("instance0");

    ASSERT_TRUE(err.IsNone());
    EXPECT_EQ(status.mState, InstanceStateEnum::eFailed);
    ASSERT_TRUE(status.mExitCode.HasValue());
    EXPECT_EQ(*status.mExitCode, 137);
    EXPECT_TRUE(status.mOOMKilled);
}

TEST_F(CRunRunnerTest, GetContainerStatusNoExitFile)
{
    auto [status, err] = mRunner.GetContainerStatus("instance0");

    ASSERT_TRUE(err.IsNone());
    EXPECT_EQ(status.mState, InstanceStateEnum::eFailed);
    EXPECT_FALSE(status.mExitCode.HasValue());
    EXPECT_FALSE(status.mOOMKilled);
}

TEST_F(CRunRunnerTest, RemoveContainerCleansPersistDir)
{
    WriteFile(mConfig.mConmonDir + "/instance0/exit", "0");

    // crun treats force deleting an unknown container as success.
    EXPECT_TRUE(mRunner.RemoveContainer("instance0").IsNone());
    EXPECT_FALSE(std::filesystem::exists(mConfig.mConmonDir + "/instance0"));
}

} // namespace aos::sm::launcher
