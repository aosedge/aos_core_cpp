/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>

#include <gtest/gtest.h>

#include "utils/channel.hpp"

using namespace testing;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST(ChannelTest, SendAndReceive)
{
    aos::common::utils::Channel<int> channel(3);

    EXPECT_EQ(channel.Send(1), ErrorEnum::eNone);
    EXPECT_EQ(channel.Send(2), ErrorEnum::eNone);
    EXPECT_EQ(channel.Send(3), ErrorEnum::eNone);

    auto result1 = channel.Receive();
    EXPECT_EQ(result1.mError, ErrorEnum::eNone);
    EXPECT_EQ(result1.mValue, 1);

    auto result2 = channel.Receive();
    EXPECT_EQ(result2.mError, ErrorEnum::eNone);
    EXPECT_EQ(result2.mValue, 2);

    auto result3 = channel.Receive();
    EXPECT_EQ(result3.mError, ErrorEnum::eNone);
    EXPECT_EQ(result3.mValue, 3);
}

TEST(ChannelTest, SendAndBlockUntilCapacity)
{
    aos::common::utils::Channel<int> channel(2);

    EXPECT_EQ(channel.Send(1), ErrorEnum::eNone);
    EXPECT_EQ(channel.Send(2), ErrorEnum::eNone);

    std::thread t([&channel]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto result = channel.Receive();
        EXPECT_EQ(result.mError, ErrorEnum::eNone);
        EXPECT_EQ(result.mValue, 1);
    });

    EXPECT_EQ(channel.Send(3), ErrorEnum::eNone);

    t.join();
}

TEST(ChannelTest, CloseAndSend)
{
    aos::common::utils::Channel<int> channel(2);

    EXPECT_EQ(channel.Send(1), ErrorEnum::eNone);
    channel.Close();

    EXPECT_EQ(channel.Send(2), ErrorEnum::eWrongState);
}

TEST(ChannelTest, CloseAndReceive)
{
    aos::common::utils::Channel<int> channel(2);

    EXPECT_EQ(channel.Send(1), ErrorEnum::eNone);
    channel.Close();

    auto result = channel.Receive();
    EXPECT_EQ(result.mError, ErrorEnum::eWrongState);
}

} // namespace aos::common::utils
