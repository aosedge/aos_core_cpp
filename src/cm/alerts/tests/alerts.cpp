/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>

#include <Poco/JSON/Object.h>

#include <gtest/gtest.h>

#include <aos/test/log.hpp>
#include <aos/test/utils.hpp>

#include <cm/alerts/alerts.hpp>

using namespace testing;

namespace aos::cm::alerts {

namespace {

class GetAlerts : public StaticVisitor<const cloudprotocol::Alerts*> {
public:
    Res Visit(const cloudprotocol::Alerts& alerts) const { return &alerts; }

    template <typename T>
    Res Visit(const T&) const
    {
        return nullptr;
    }
};

class CommunicationStub : public communication::CommunicationItf {
public:
    Error SendMessage(const cloudprotocol::MessageVariant& body) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Send message called";

        mMessages.push_back(body);
        mCondVar.notify_all();

        return ErrorEnum::eNone;
    }

    bool WaitForMessage(
        cloudprotocol::MessageVariant& message, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};

        if (!mCondVar.wait_for(lock, timeout, [&]() { return !mMessages.empty(); })) {
            return false;
        }

        message = std::move(mMessages.front());
        mMessages.erase(mMessages.begin());

        return true;
    }

    std::vector<cloudprotocol::MessageVariant> GetMessages()
    {
        std::lock_guard lock {mMutex};

        return mMessages;
    }

private:
    std::mutex                                 mMutex;
    std::condition_variable                    mCondVar;
    std::vector<cloudprotocol::MessageVariant> mMessages;
};

std::unique_ptr<cloudprotocol::AlertVariant> CreateSystemAlert(
    const Time& timestamp, const std::string& nodeID = "node1", const std::string& message = "test message")
{
    auto alert      = std::make_unique<cloudprotocol::SystemAlert>(timestamp);
    alert->mNodeID  = nodeID.c_str();
    alert->mMessage = message.c_str();

    return std::make_unique<cloudprotocol::AlertVariant>(*alert);
}

std::unique_ptr<cloudprotocol::AlertVariant> CreateCoreAlert(
    const Time& timestamp, const std::string& nodeID = "node1", const std::string& message = "test message")
{
    auto alert      = std::make_unique<cloudprotocol::CoreAlert>(timestamp);
    alert->mNodeID  = nodeID.c_str();
    alert->mMessage = message.c_str();

    return std::make_unique<cloudprotocol::AlertVariant>(*alert);
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CMAlerts : public Test {
protected:
    void SetUp() override
    {
        test::InitLog();

        mConfig.mSendPeriod         = Time::cSeconds * 1;
        mConfig.mMaxMessageSize     = 2 * 1024;
        mConfig.mMaxOfflineMessages = 10;

        auto err = mAlerts.Init(mConfig, mCommunication);
        ASSERT_TRUE(err.IsNone()) << aos::test::ErrorToStr(err);
    }

    config::Alerts    mConfig {};
    CommunicationStub mCommunication;
    Alerts            mAlerts;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CMAlerts, DuplicatesAreSkipped)
{
    const auto     cTime                    = Time::Now();
    constexpr auto cExpectedSentAlertsCount = 3;

    const std::array cAlerts = {
        CreateSystemAlert(cTime, "node1", "test message 1"),
        CreateSystemAlert(cTime, "node1", "test message 1"),
        CreateSystemAlert(cTime, "node1", "test message 1"),
        CreateCoreAlert(cTime, "node1", "test message 2"),
        CreateCoreAlert(cTime.Add(Time::cSeconds), "node1", "test message 2"),
        CreateCoreAlert(cTime.Add(Time::cSeconds), "node2", "test message 3"),
    };

    mAlerts.OnConnect();

    auto err = mAlerts.Start();
    ASSERT_TRUE(err.IsNone()) << aos::test::ErrorToStr(err);

    for (const auto& alert : cAlerts) {
        err = mAlerts.SendAlert(*alert);
        EXPECT_TRUE(err.IsNone()) << aos::test::ErrorToStr(err);
    }

    auto msg = std::make_unique<cloudprotocol::MessageVariant>();
    EXPECT_TRUE(mCommunication.WaitForMessage(*msg));

    auto sentAlerts = msg->ApplyVisitor(GetAlerts());

    EXPECT_EQ((sentAlerts) ? sentAlerts->mItems.Size() : 0, cExpectedSentAlertsCount);

    err = mAlerts.Stop();
    ASSERT_TRUE(err.IsNone()) << aos::test::ErrorToStr(err);
}

TEST_F(CMAlerts, OfflineMessagesThresholdIsApplied)
{
    const auto cExpectedSentAlertsCount
        = static_cast<size_t>(mConfig.mMaxOfflineMessages) * cloudprotocol::cAlertItemsCount;
    const auto cTime = Time::Now();

    std::vector<std::unique_ptr<cloudprotocol::AlertVariant>> alerts;

    for (size_t i = 0; i < cExpectedSentAlertsCount; ++i) {
        auto alert = CreateSystemAlert(cTime, "node1", "test message " + std::to_string(i));

        auto err = mAlerts.SendAlert(*alert);
        EXPECT_TRUE(err.IsNone()) << aos::test::ErrorToStr(err);
    }

    auto alert = CreateSystemAlert(cTime, "node1",
        "test message " + std::to_string(mConfig.mMaxOfflineMessages * cloudprotocol::cAlertItemsCount));

    auto err = mAlerts.SendAlert(*alert);
    EXPECT_TRUE(err.Is(ErrorEnum::eNoMemory)) << aos::test::ErrorToStr(err);

    err = mAlerts.Start();
    EXPECT_TRUE(err.IsNone()) << aos::test::ErrorToStr(err);

    auto msg = std::make_unique<cloudprotocol::MessageVariant>();
    EXPECT_FALSE(mCommunication.WaitForMessage(*msg));

    mAlerts.OnConnect();

    std::vector<cloudprotocol::Alerts> receivedAlerts;

    while (mCommunication.WaitForMessage(*msg)) {
        if (auto alerts = msg->ApplyVisitor(GetAlerts()); alerts) {
            receivedAlerts.push_back(*alerts);
        }
    }

    EXPECT_EQ(receivedAlerts.size(), mConfig.mMaxOfflineMessages);

    const auto totalReceivedAlertsCount = std::accumulate(receivedAlerts.begin(), receivedAlerts.end(), 0,
        [](size_t sum, const cloudprotocol::Alerts& alerts) { return sum + alerts.mItems.Size(); });

    EXPECT_EQ(totalReceivedAlertsCount, cExpectedSentAlertsCount);

    err = mAlerts.Stop();
    ASSERT_TRUE(err.IsNone()) << aos::test::ErrorToStr(err);
}

} // namespace aos::cm::alerts
