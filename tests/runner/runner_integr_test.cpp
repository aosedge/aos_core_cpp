/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <gmock/gmock.h>

#include <aos/test/log.hpp>

#include "runner/runner.hpp"

using namespace testing;

namespace aos::sm::runner {

namespace {

std::filesystem::path GetRuntimeDir()
{
    const auto testDir = std::filesystem::canonical("/proc/self/exe").parent_path();

    return testDir / "runtime";
}

class RunStatusReceiverStub : public RunStatusReceiverItf {
public:
    Error UpdateRunStatus(const Array<RunStatus>& units) override
    {
        LOG_INF() << "OnUnitChanged: units num=" << units.Size();

        for (const auto& unit : units) {
            LOG_INF() << "OnUnitChanged: id=" << unit.mInstanceID << ", state=" << unit.mState.ToString()
                      << ", err=" << unit.mError;
        }

        return ErrorEnum::eNone;
    }
};

} // namespace

class RunnerIntTest : public Test {
public:
    void SetUp() override
    {
        test::InitLog();

        const auto svcPath    = std::filesystem::canonical(__FILE__).parent_path() / "aos-service@.service";
        const auto scriptPath = std::filesystem::canonical(__FILE__).parent_path() / "hello-world.sh";

        if (!std::filesystem::exists(GetRuntimeDir())) {
            std::filesystem::create_directories(GetRuntimeDir());
        }

        if (!std::filesystem::exists("/lib/systemd/system/aos-service@.service")) {
            std::filesystem::copy_file(svcPath, "/lib/systemd/system/aos-service@.service");
        }

        if (!std::filesystem::exists("/opt/hello-world.sh")) {
            std::filesystem::copy_file(scriptPath, "/opt/hello-world.sh");
        }

        mRunner.Init(mRunStatusReceiver);
        mRunner.Start();
    }

    void TearDown() override
    {
        if (!std::filesystem::exists(GetRuntimeDir())) {
            std::filesystem::remove_all(GetRuntimeDir());
        }

        if (!std::filesystem::exists("/lib/systemd/system/aos-service@.service")) {
            std::filesystem::remove_all("/lib/systemd/system/aos-service@.service");
        }

        if (!std::filesystem::exists("/opt/hello-world.sh")) {
            std::filesystem::remove_all("/opt/hello-world.sh");
        }
    }

protected:
    const std::filesystem::path cRuntimeDir = GetRuntimeDir();

    RunStatusReceiverStub mRunStatusReceiver;
    Runner                mRunner;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RunnerIntTest, DISABLED_StartInstance)
{
    RunParameters params   = {};
    auto          instance = mRunner.StartInstance("service0", cRuntimeDir.c_str(), params);

    LOG_INF() << "id=" << instance.mInstanceID << ", state=" << instance.mState.ToString()
              << ", err=" << instance.mError;

    const auto expectedRes = RunStatus {"service0", InstanceRunStateEnum::eActive, Error()};

    EXPECT_EQ(instance, expectedRes);
    EXPECT_TRUE(mRunner.StopInstance("service0").IsNone());

    mRunner.Stop();
}

} // namespace aos::sm::runner
