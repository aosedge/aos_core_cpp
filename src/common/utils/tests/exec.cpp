/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/wait.h>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <common/utils/exec.hpp>

using namespace testing;

namespace aos::common::utils {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

int WaitExitCode(pid_t pid)
{
    int status = 0;

    EXPECT_EQ(waitpid(pid, &status, 0), pid);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

class ExecTest : public Test {
public:
    static void SetUpTestSuite() { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ExecTest, ExecAsyncCommandReturnsPidOfSpawnedProcess)
{
    auto [pid, err] = ExecAsyncCommand({"/bin/sh", "-c", "exit 0"});

    ASSERT_TRUE(err.IsNone());
    ASSERT_GT(pid, 0);

    EXPECT_EQ(WaitExitCode(pid), 0);
}

TEST_F(ExecTest, ExecAsyncCommandDoesNotWaitForExit)
{
    auto [pid, err] = ExecAsyncCommand({"/bin/sh", "-c", "sleep 1"});

    ASSERT_TRUE(err.IsNone());
    ASSERT_GT(pid, 0);

    // Unlike ExecDetachedCommand, this call must return before the child exits.
    EXPECT_EQ(waitpid(pid, nullptr, WNOHANG), 0);

    EXPECT_EQ(WaitExitCode(pid), 0);
}

TEST_F(ExecTest, ExecAsyncCommandPropagatesNonZeroExitCode)
{
    auto [pid, err] = ExecAsyncCommand({"/bin/sh", "-c", "exit 7"});

    ASSERT_TRUE(err.IsNone());
    ASSERT_GT(pid, 0);

    EXPECT_EQ(WaitExitCode(pid), 7);
}

TEST_F(ExecTest, ExecAsyncCommandInvalidExecutableReturnsError)
{
    auto [pid, err] = ExecAsyncCommand({"/no/such/executable"});

    EXPECT_FALSE(err.IsNone());
    EXPECT_EQ(pid, -1);
}

TEST_F(ExecTest, ExecAsyncCommandEmptyArgsReturnsError)
{
    auto [pid, err] = ExecAsyncCommand({});

    EXPECT_TRUE(err.Is(ErrorEnum::eInvalidArgument));
    EXPECT_EQ(pid, -1);
}

} // namespace aos::common::utils
