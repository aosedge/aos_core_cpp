/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <future>

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/launcher/runtimes/container/runner.hpp>

#include "mocks/containerrunnermock.hpp"
#include "mocks/runnermock.hpp"

using namespace testing;

namespace aos::sm::launcher {

class ContainerRunnerTest : public Test {
public:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override { mRunner.Init(mRunStatusReceiver, mContainerRunnerMock); }

protected:
    RunStatusReceiverMock           mRunStatusReceiver;
    StrictMock<ContainerRunnerMock> mContainerRunnerMock;
    Runner                          mRunner;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ContainerRunnerTest, StartInstance)
{
    RunParameters   params = {{500 * Time::cMilliseconds}, {0}, {0}};
    ContainerStatus status = {"service0", InstanceStateEnum::eActive, {}};
    Error           err    = ErrorEnum::eNone;

    EXPECT_CALL(mContainerRunnerMock, StartContainer("service0")).WillOnce(Return(err));
    EXPECT_CALL(mContainerRunnerMock, GetContainerStatus("service0"))
        .WillOnce(Return(RetWithError<ContainerStatus>(status, err)));

    std::vector<ContainerStatus> statuses = {status};
    EXPECT_CALL(mContainerRunnerMock, ListContainers())
        .WillRepeatedly(Return(RetWithError<std::vector<ContainerStatus>>(statuses, err)));

    std::promise<void> updateRunStatusCalled;

    std::vector<RunStatus> expectedInstances {{"service0", InstanceStateEnum::eActive, Error()}};
    EXPECT_CALL(mRunStatusReceiver, UpdateRunStatus(expectedInstances))
        .WillOnce(InvokeWithoutArgs([&updateRunStatusCalled]() {
            updateRunStatusCalled.set_value();
            return true;
        }));

    mRunner.Start();

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eActive, ErrorEnum::eNone};

    EXPECT_EQ(mRunner.StartInstance("service0", params), expectedRes);

    EXPECT_TRUE(updateRunStatusCalled.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);

    EXPECT_CALL(mContainerRunnerMock, StopContainer("service0")).WillOnce(Return(err));
    EXPECT_CALL(mContainerRunnerMock, RemoveContainer("service0")).WillOnce(Return(err));

    EXPECT_TRUE(mRunner.StopInstance("service0").IsNone());

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, StartContainerFailed)
{
    RunParameters params = {};

    EXPECT_CALL(mContainerRunnerMock, StartContainer("service0")).WillOnce(Return(ErrorEnum::eFailed));

    mRunner.Start();

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", params), expectedRes);

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, GetContainerStatusFailed)
{
    mRunner.Start();

    RunParameters   params = {};
    ContainerStatus status = {"service0", InstanceStateEnum::eFailed, {1}};
    Error           err    = ErrorEnum::eFailed;

    EXPECT_CALL(mContainerRunnerMock, StartContainer("service0")).WillOnce(Return(Error()));
    EXPECT_CALL(mContainerRunnerMock, GetContainerStatus("service0"))
        .WillOnce(Return(RetWithError<ContainerStatus>(status, err)));

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", params), expectedRes);

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, ListContainersFailed)
{
    mRunner.Start();

    RunParameters params = {};

    EXPECT_CALL(mContainerRunnerMock, StartContainer("service0")).WillOnce(Return(ErrorEnum::eFailed));

    const auto expectedRes = RunStatus {"service0", InstanceStateEnum::eFailed, ErrorEnum::eFailed};

    EXPECT_EQ(mRunner.StartInstance("service0", params), expectedRes);

    std::vector<ContainerStatus> statuses = {{"service0", InstanceStateEnum::eFailed, {1}}};

    std::promise<void> listContainersCalled;

    EXPECT_CALL(mContainerRunnerMock, ListContainers())
        .WillOnce(InvokeWithoutArgs([&listContainersCalled, &statuses]() -> RetWithError<std::vector<ContainerStatus>> {
            listContainersCalled.set_value();

            return RetWithError<std::vector<ContainerStatus>>(statuses, Error(ErrorEnum::eFailed));
        }));

    EXPECT_TRUE(listContainersCalled.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, RestartOnContainerFailure)
{
    // startInterval=500ms (burst window), restartInterval=0ms, burst=3
    RunParameters params = {{500 * Time::cMilliseconds}, {0}, {3}};

    Error           err          = ErrorEnum::eNone;
    ContainerStatus activeStatus = {"service0", InstanceStateEnum::eActive, {}};
    ContainerStatus failedStatus = {"service0", InstanceStateEnum::eFailed, {}};

    std::promise<void> restartedPromise;
    EXPECT_CALL(mContainerRunnerMock, StartContainer("service0"))
        .WillOnce(Return(err))
        .WillOnce(InvokeWithoutArgs([&restartedPromise, err]() -> Error {
            restartedPromise.set_value();
            return err;
        }));

    EXPECT_CALL(mContainerRunnerMock, GetContainerStatus("service0"))
        .WillOnce(Return(RetWithError<ContainerStatus>(activeStatus, err)));

    std::vector<ContainerStatus> activeStatuses = {activeStatus};
    std::vector<ContainerStatus> failedStatuses = {failedStatus};
    EXPECT_CALL(mContainerRunnerMock, ListContainers())
        .WillOnce(Return(RetWithError<std::vector<ContainerStatus>>(activeStatuses, err)))
        .WillOnce(Return(RetWithError<std::vector<ContainerStatus>>(failedStatuses, err)))
        .WillRepeatedly(Return(RetWithError<std::vector<ContainerStatus>>(activeStatuses, err)));

    // RemoveContainer: once from restart logic, once from StopInstance
    EXPECT_CALL(mContainerRunnerMock, RemoveContainer("service0")).Times(2).WillRepeatedly(Return(err));
    EXPECT_CALL(mContainerRunnerMock, StopContainer("service0")).WillOnce(Return(err));

    EXPECT_CALL(mRunStatusReceiver, UpdateRunStatus(_)).WillRepeatedly(Return(Error()));

    mRunner.Start();

    EXPECT_EQ(mRunner.StartInstance("service0", params).mState, InstanceStateEnum::eActive);

    EXPECT_TRUE(restartedPromise.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);

    EXPECT_TRUE(mRunner.StopInstance("service0").IsNone());

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, StopInstanceWaitsForInFlightRestart)
{
    // Regression test: StopInstance must block while a restart for the same instance is in flight
    // (so the container isn't resurrected right after being stopped), but must NOT deadlock once
    // the restart settles.
    RunParameters params = {{500 * Time::cMilliseconds}, {0}, {0}};

    Error           err          = ErrorEnum::eNone;
    ContainerStatus activeStatus = {"service0", InstanceStateEnum::eActive, {}};
    ContainerStatus failedStatus = {"service0", InstanceStateEnum::eFailed, {}};

    // Exactly 2 StartContainer calls are expected: the initial start and the one restart.
    // If StopInstance let a second restart race in after being asked to stop, this would be
    // called a 3rd time and the strict mock would fail the test.
    EXPECT_CALL(mContainerRunnerMock, StartContainer("service0")).Times(2).WillRepeatedly(Return(err));

    EXPECT_CALL(mContainerRunnerMock, GetContainerStatus("service0"))
        .WillOnce(Return(RetWithError<ContainerStatus>(activeStatus, err)));

    std::vector<ContainerStatus> activeStatuses = {activeStatus};
    std::vector<ContainerStatus> failedStatuses = {failedStatus};
    EXPECT_CALL(mContainerRunnerMock, ListContainers())
        .WillOnce(Return(RetWithError<std::vector<ContainerStatus>>(activeStatuses, err)))
        .WillOnce(Return(RetWithError<std::vector<ContainerStatus>>(failedStatuses, err)))
        .WillRepeatedly(Return(RetWithError<std::vector<ContainerStatus>>(activeStatuses, err)));

    std::promise<void>       removeStartedPromise;
    std::promise<void>       proceedPromise;
    std::shared_future<void> proceedFuture = proceedPromise.get_future().share();

    // RemoveContainer: 1st call is the monitor's restart (held open to force the race window),
    // 2nd call is StopInstance's own cleanup.
    EXPECT_CALL(mContainerRunnerMock, RemoveContainer("service0"))
        .WillOnce(InvokeWithoutArgs([&removeStartedPromise, proceedFuture, err]() -> Error {
            removeStartedPromise.set_value();
            proceedFuture.wait();

            return err;
        }))
        .WillOnce(Return(err));

    EXPECT_CALL(mContainerRunnerMock, StopContainer("service0")).WillOnce(Return(err));

    EXPECT_CALL(mRunStatusReceiver, UpdateRunStatus(_)).WillRepeatedly(Return(Error()));

    mRunner.Start();

    EXPECT_EQ(mRunner.StartInstance("service0", params).mState, InstanceStateEnum::eActive);

    // Wait until the monitor thread is blocked inside the in-flight restart.
    ASSERT_EQ(removeStartedPromise.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);

    std::promise<Error> stopResultPromise;
    std::thread         stopThread([&]() { stopResultPromise.set_value(mRunner.StopInstance("service0")); });

    auto stopFuture = stopResultPromise.get_future();

    // StopInstance must not complete while the restart is still in flight.
    EXPECT_EQ(stopFuture.wait_for(std::chrono::milliseconds(300)), std::future_status::timeout);

    // Let the in-flight restart finish.
    proceedPromise.set_value();

    // StopInstance must complete promptly afterwards, not deadlock.
    ASSERT_EQ(stopFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(stopFuture.get().IsNone());

    stopThread.join();

    mRunner.Stop();
}

TEST_F(ContainerRunnerTest, RestartBurstLimitExceeded)
{
    RunParameters params = {{10 * Time::cSeconds}, {1 * Time::cSeconds}, {1}};

    Error           err          = ErrorEnum::eNone;
    ContainerStatus activeStatus = {"service0", InstanceStateEnum::eActive, {}};
    ContainerStatus failedStatus = {"service0", InstanceStateEnum::eFailed, {}};

    std::promise<void> restartedPromise;

    // StartContainer called exactly twice: initial start + 1 restart (burst=1 blocks further restarts)
    EXPECT_CALL(mContainerRunnerMock, StartContainer("service0"))
        .WillOnce(Return(err))
        .WillOnce(InvokeWithoutArgs([&restartedPromise, err]() -> Error {
            restartedPromise.set_value();
            return err;
        }));

    EXPECT_CALL(mContainerRunnerMock, GetContainerStatus("service0"))
        .WillOnce(Return(RetWithError<ContainerStatus>(activeStatus, err)));

    std::vector<ContainerStatus> activeStatuses = {activeStatus};
    std::vector<ContainerStatus> failedStatuses = {failedStatus};
    // Sequence: active → failed(restart) → active → failed(burst exceeded) → active…
    EXPECT_CALL(mContainerRunnerMock, ListContainers())
        .WillRepeatedly(Return(RetWithError<std::vector<ContainerStatus>>(failedStatuses, err)));

    // RemoveContainer: once from restart logic, once from StopInstance
    EXPECT_CALL(mContainerRunnerMock, RemoveContainer("service0")).Times(2).WillRepeatedly(Return(err));
    EXPECT_CALL(mContainerRunnerMock, StopContainer("service0")).WillOnce(Return(err));

    EXPECT_CALL(mRunStatusReceiver, UpdateRunStatus(_)).WillRepeatedly(Return(Error()));

    mRunner.Start();

    EXPECT_EQ(mRunner.StartInstance("service0", params).mState, InstanceStateEnum::eActive);

    // Wait for the single allowed restart
    EXPECT_TRUE(restartedPromise.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);

    // Allow 3 more monitoring cycles to pass (covering the second failure + burst-limited non-restart)
    sleep(3);

    EXPECT_TRUE(mRunner.StopInstance("service0").IsNone());

    mRunner.Stop();
}

} // namespace aos::sm::launcher
