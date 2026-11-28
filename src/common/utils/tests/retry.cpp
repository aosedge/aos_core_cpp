/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <future>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <common/utils/retry.hpp>

using namespace testing;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class RetryTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RetryTest, ReturnsNoneOnSuccess)
{
    auto attempts = 0;

    auto err = Retry(
        [&attempts]() -> Error {
            if (++attempts < 3) {
                return ErrorEnum::eFailed;
            }

            return ErrorEnum::eNone;
        },
        nullptr, 3, Time::cMilliseconds);

    EXPECT_TRUE(err.IsNone());
    EXPECT_EQ(attempts, 3);
}

TEST_F(RetryTest, ReturnsLastErrorWhenTriesExceeded)
{
    auto attempts  = 0;
    auto callbacks = 0;

    auto err = Retry(
        [&attempts]() -> Error {
            attempts++;

            return ErrorEnum::eFailed;
        },
        [&callbacks](int, Duration, Error) { callbacks++; }, 3, Time::cMilliseconds);

    EXPECT_TRUE(err.Is(ErrorEnum::eFailed));
    EXPECT_EQ(attempts, 3);
    EXPECT_EQ(callbacks, 2);
}

TEST_F(RetryTest, CancelInterruptsDelay)
{
    CancelContext      ctx;
    std::promise<void> firstAttempt;

    auto future = std::async(std::launch::async, [&]() {
        auto attempts = 0;

        return Retry(
            ctx,
            [&]() -> Error {
                if (++attempts == 1) {
                    firstAttempt.set_value();
                }

                return ErrorEnum::eFailed;
            },
            nullptr, 0, Time::cMinutes);
    });

    firstAttempt.get_future().wait();

    ASSERT_TRUE(ctx.Cancel().IsNone());

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(future.get().Is(ErrorEnum::eCanceled));
}

} // namespace aos::common::utils
