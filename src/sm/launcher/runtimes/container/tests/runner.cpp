/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/launcher/runtimes/container/runner.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

#include "mocks/runnermock.hpp"

using namespace testing;

namespace aos::sm::launcher {

class TestRunner : public Runner {
public:
private:
    std::string GetSystemdDropInsDir() const override
    {
        const auto testDir = std::filesystem::canonical("/proc/self/exe").parent_path();

        return testDir / "systemd";
    }
};

class ContainerRunnerTest : public Test {
public:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override { mRunner.Init(mRunStatusReceiver, mSystemdMock); }

protected:
    RunStatusReceiverMock  mRunStatusReceiver;
    utils::SystemdConnMock mSystemdMock;
    TestRunner             mRunner;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ContainerRunnerTest, StartInstance)
{
    RunParameters     params = {{500 * Time::cMilliseconds}, {0}, {0}};
    utils::UnitStatus status = {"aos-service@service0.service", utils::UnitStateEnum::eActive, 0};
    Error             err    = ErrorEnum::eNone;

    EXPECT_CALL(mSystemdMock, StartUnit("aos-service@service0.service", "replace", _)).WillOnce(Return(err));
    EXPECT_CALL(mSystemdMock, GetUnitStatus(_)).WillOnce(Return(RetWithError<utils::UnitStatus>(status, err)));

    std::vector<utils::UnitStatus> units = {status};
    EXPECT_CALL(mSystemdMock, ListUnits())
        .WillRepeatedly(Return(RetWithError<std::vector<utils::UnitStatus>>(units, err)));
    std::vector<RunStatus> expectedInstances {{"service0", InstanceStateEnum::eActive, Error()}};

    EXPECT_CALL(mRunStatusReceiver, UpdateRunStatus(expectedInstances)).Times(1);

    mRunner.Start();

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eActive, ErrorEnum::eNone};

    EXPECT_EQ(mRunner.StartInstance("service0", params), expectedRes);

    sleep(2); // wait to monitor

    EXPECT_CALL(mSystemdMock, StopUnit("aos-service@service0.service", "replace", _)).WillOnce(Return(err));
    EXPECT_CALL(mSystemdMock, ResetFailedUnit("aos-service@service0.service")).WillOnce(Return(err));

    EXPECT_TRUE(mRunner.StopInstance("service0").IsNone());

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, StartUnitFailed)
{
    RunParameters params = {};

    EXPECT_CALL(mSystemdMock, StartUnit("aos-service@service0.service", "replace", _))
        .WillOnce(Return(ErrorEnum::eFailed));

    mRunner.Start();

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", params), expectedRes);

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, GetUnitStatusFailed)
{
    mRunner.Start();

    RunParameters     params = {};
    utils::UnitStatus status = {"aos-service@service0.service", utils::UnitStateEnum::eFailed, 1};
    Error             err    = ErrorEnum::eFailed;

    EXPECT_CALL(mSystemdMock, StartUnit("aos-service@service0.service", "replace", _)).WillOnce(Return(Error()));
    EXPECT_CALL(mSystemdMock, GetUnitStatus("aos-service@service0.service"))
        .WillOnce(Return(RetWithError<utils::UnitStatus>(status, err)));

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", params), expectedRes);

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, ListUnitsFailed)
{
    mRunner.Start();

    RunParameters params = {};

    EXPECT_CALL(mSystemdMock, StartUnit("aos-service@service0.service", "replace", _))
        .WillOnce(Return(ErrorEnum::eFailed));

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", params), expectedRes);

    utils::UnitStatus              status = {"aos-service@service0.service", utils::UnitStateEnum::eFailed, 1};
    std::vector<utils::UnitStatus> units  = {status};

    EXPECT_CALL(mSystemdMock, ListUnits())
        .WillOnce(Return(RetWithError<std::vector<utils::UnitStatus>>(units, Error(ErrorEnum::eFailed))));
    sleep(2); // wait to monitor

    mRunner.Stop();
}

} // namespace aos::sm::launcher
