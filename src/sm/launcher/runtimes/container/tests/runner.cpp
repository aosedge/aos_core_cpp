/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/launcher/runtimes/container/runner.hpp>

#include "mocks/runstatusreceivermock.hpp"
#include "mocks/systemdconnmock.hpp"

using namespace testing;

namespace aos::sm::runner {

namespace {

std::filesystem::path GetRuntimeDir()
{
    const auto testDir = std::filesystem::canonical("/proc/self/exe").parent_path();

    return testDir / "runtime";
}

} // namespace

class TestRunner : public Runner {
private:
    std::shared_ptr<SystemdConnItf> CreateSystemdConn() override { return mSystemd; }

    std::string GetSystemdDropInsDir() const override
    {
        const auto testDir = std::filesystem::canonical("/proc/self/exe").parent_path();

        return testDir / "systemd";
    }

    std::shared_ptr<SystemdConnMock> mSystemd = std::make_shared<SystemdConnMock>();
};

class RunnerTest : public Test {
public:
    void SetUp() override
    {
        tests::utils::InitLog();

        mRunner.Init(mRunStatusReceiver);
    }

protected:
    const std::filesystem::path cRuntimeDir = GetRuntimeDir();

    RunStatusReceiverMock mRunStatusReceiver;

    TestRunner mRunner;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RunnerTest, StartInstance)
{
    RunParameters params = {{500 * Time::cMilliseconds}, {0}, {0}};
    UnitStatus    status = {"aos-service@service0.service", UnitStateEnum::eActive, 0};
    Error         err    = ErrorEnum::eNone;

    EXPECT_CALL(*mRunner.mSystemd, StartUnit("aos-service@service0.service", "replace", _)).WillOnce(Return(err));
    EXPECT_CALL(*mRunner.mSystemd, GetUnitStatus(_)).WillOnce(Return(RetWithError<UnitStatus>(status, err)));

    std::vector<UnitStatus> units = {status};
    EXPECT_CALL(*mRunner.mSystemd, ListUnits())
        .WillRepeatedly(Return(RetWithError<std::vector<UnitStatus>>(units, err)));

    StaticArray<RunStatus, 1> expectedInstances;

    expectedInstances.PushBack(RunStatus {"service0", InstanceStateEnum::eActive, Error()});

    EXPECT_CALL(mRunStatusReceiver, UpdateRunStatus(expectedInstances)).Times(1);

    mRunner.Start();

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eActive, ErrorEnum::eNone};

    EXPECT_EQ(mRunner.StartInstance("service0", cRuntimeDir.c_str(), params), expectedRes);

    sleep(2); // wait to monitor

    EXPECT_CALL(*mRunner.mSystemd, StopUnit("aos-service@service0.service", "replace", _)).WillOnce(Return(err));
    EXPECT_CALL(*mRunner.mSystemd, ResetFailedUnit("aos-service@service0.service")).WillOnce(Return(err));

    EXPECT_TRUE(mRunner.StopInstance("service0").IsNone());

    mRunner.Stop();
}

TEST_F(RunnerTest, StartUnitFailed)
{
    RunParameters params = {};

    EXPECT_CALL(*mRunner.mSystemd, StartUnit("aos-service@service0.service", "replace", _))
        .WillOnce(Return(ErrorEnum::eFailed));

    mRunner.Start();

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", cRuntimeDir.c_str(), params), expectedRes);

    mRunner.Stop();
}

TEST_F(RunnerTest, GetUnitStatusFailed)
{
    mRunner.Start();

    RunParameters params = {};
    UnitStatus    status = {"aos-service@service0.service", UnitStateEnum::eFailed, 1};
    Error         err    = ErrorEnum::eFailed;

    EXPECT_CALL(*mRunner.mSystemd, StartUnit("aos-service@service0.service", "replace", _)).WillOnce(Return(Error()));
    EXPECT_CALL(*mRunner.mSystemd, GetUnitStatus("aos-service@service0.service"))
        .WillOnce(Return(RetWithError<UnitStatus>(status, err)));

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", cRuntimeDir.c_str(), params), expectedRes);

    mRunner.Stop();
}

TEST_F(RunnerTest, ListUnitsFailed)
{
    mRunner.Start();

    RunParameters params = {};

    EXPECT_CALL(*mRunner.mSystemd, StartUnit("aos-service@service0.service", "replace", _))
        .WillOnce(Return(ErrorEnum::eFailed));

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", cRuntimeDir.c_str(), params), expectedRes);

    UnitStatus              status = {"aos-service@service0.service", UnitStateEnum::eFailed, 1};
    std::vector<UnitStatus> units  = {status};

    EXPECT_CALL(*mRunner.mSystemd, ListUnits())
        .WillOnce(Return(RetWithError<std::vector<UnitStatus>>(units, Error(ErrorEnum::eFailed))));
    sleep(2); // wait to monitor

    mRunner.Stop();
}

} // namespace aos::sm::runner
