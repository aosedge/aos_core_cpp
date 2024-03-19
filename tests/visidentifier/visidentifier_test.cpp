/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include "logger/logger.hpp"
#include "mocks/vissubjectsobservermock.hpp"
#include "mocks/wsclientmock.hpp"
#include "visidentifier/pocowsclient.hpp"
#include "visidentifier/visidentifier.hpp"
#include "visidentifier/vismessage.hpp"
#include "visidentifier/wsexception.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class TestVISIdentifier : public VISIdentifier {

public:
    void           SetWSClient(WSClientItfPtr wsClient) { VISIdentifier::SetWSClient(wsClient); }
    WSClientItfPtr GetWSClient() { return VISIdentifier::GetWSClient(); }
    void           HandleSubscription(const std::string& message) { return VISIdentifier::HandleSubscription(message); }
    void           WaitUntilConnected() { VISIdentifier::WaitUntilConnected(); }

    MOCK_METHOD(aos::Error, InitWSClient, (const Config&), (override));
};

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class VisidentifierTest : public testing::Test {
protected:
    const std::string cTestSubscriptionId {"1234-4321"};
    const VISConfig   cVISConfig {"vis-service", "ca-path", UtilsTime::Duration(1)};

    WSClientEvent              mWSClientEvent;
    VisSubjectsObserverMockPtr mVisSubjectsObserverMockPtr {std::make_shared<StrictMock<VisSubjectsObserverMock>>()};
    WSClientMockPtr            mWSClientItfMockPtr {std::make_shared<StrictMock<WSClientMock>>()};
    TestVISIdentifier          mVisIdentifier;
    Config                     mConfig;

    // This method is called before any test cases in the test suite
    static void SetUpTestSuite()
    {
        static Logger mLogger;

        mLogger.SetBackend(Logger::Backend::eStdIO);
        mLogger.SetLogLevel(aos::LogLevelEnum::eDebug);
        mLogger.Init();
    }

    void SetUp() override
    {
        mConfig.mIdentifier.mParams = cVISConfig.ToJSON();

        mVisIdentifier.SetWSClient(mWSClientItfMockPtr);
    }

    void TearDown() override
    {
        if (mVisIdentifier.GetWSClient() != nullptr) {
            ExpectUnsubscribeAllIsSent();

            // ws closed
            EXPECT_CALL(*mWSClientItfMockPtr, Close).WillOnce(Invoke([this] {
                mWSClientEvent.Set(WSClientEvent::EventEnum::CLOSED, "mock closed");
            }));
        }
    }

    void ExpectSubscribeSucceeded()
    {
        EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
        EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
            .Times(1)
            .WillOnce(
                Invoke([this](const std::string&, const WSClientItf::ByteArray& message) -> WSClientItf::ByteArray {
                    try {
                        const VISMessage request(std::string {message.cbegin(), message.cend()});

                        EXPECT_TRUE(request.Is(VISAction::EnumType::eSubscribe)) << request.ToString();

                        VISMessage subscribeResponse(VISActionEnum::eSubscribe);

                        subscribeResponse.SetKeyValue("requestId", "request-id");
                        subscribeResponse.SetKeyValue("subscriptionId", cTestSubscriptionId);

                        const auto str = subscribeResponse.ToString();

                        return {str.cbegin(), str.cend()};
                    } catch (...) {
                        return {};
                    }
                }));
    }

    void ExpectInitSucceeded()
    {
        mVisIdentifier.SetWSClient(mWSClientItfMockPtr);

        ExpectSubscribeSucceeded();
        EXPECT_CALL(*mWSClientItfMockPtr, Connect).Times(1);
        EXPECT_CALL(mVisIdentifier, InitWSClient).WillOnce(Return(aos::ErrorEnum::eNone));
        EXPECT_CALL(*mWSClientItfMockPtr, WaitForEvent).WillOnce(Invoke([this]() { return mWSClientEvent.Wait(); }));

        const auto err = mVisIdentifier.Init(mConfig, mVisSubjectsObserverMockPtr);
        ASSERT_TRUE(err.IsNone()) << err.Message();

        mVisIdentifier.WaitUntilConnected();
    }

    void ExpectUnsubscribeAllIsSent()
    {
        EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
        EXPECT_CALL(*mWSClientItfMockPtr, AsyncSendMessage)
            .Times(1)
            .WillOnce(Invoke([&](const WSClientItf::ByteArray& message) {
                try {
                    VISMessage visMessage(std::string {message.cbegin(), message.cend()});

                    ASSERT_TRUE(visMessage.Is(VISAction::EnumType::eUnsubscribeAll));
                } catch (...) {
                    FAIL() << "exception was not expected";
                }
            }));
    }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(VisidentifierTest, InitFailsOnEmptyConfig)
{
    VISIdentifier identifier;

    const auto err = identifier.Init(Config {}, mVisSubjectsObserverMockPtr);
    ASSERT_FALSE(err.IsNone()) << err.Message();
}

TEST_F(VisidentifierTest, InitSubjectsObserverNotAccessible)
{
    mVisSubjectsObserverMockPtr.reset();

    EXPECT_CALL(mVisIdentifier, InitWSClient).Times(0);

    const auto err = mVisIdentifier.Init(mConfig, mVisSubjectsObserverMockPtr);
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eInvalidArgument)) << err.Message();
}

TEST_F(VisidentifierTest, SubscriptionNotificationReceivedAndObserverIsNotified)
{
    ExpectInitSucceeded();

    aos::StaticArray<aos::StaticString<aos::cSubjectIDLen>, 3> subjects;

    EXPECT_CALL(*mVisSubjectsObserverMockPtr, SubjectsChanged)
        .Times(1)
        .WillOnce(Invoke([&subjects](const auto& newSubjects) {
            subjects = newSubjects;

            return aos::ErrorEnum::eNone;
        }));

    const std::string kSubscriptionNofiticationJson
        = R"({"action":"subscription","subscriptionId":"1234-4321","value":[11,12,13], "timestamp": 0})";

    mVisIdentifier.HandleSubscription(kSubscriptionNofiticationJson);

    EXPECT_EQ(subjects.Size(), 3);

    // Observer is notified only if subsription json contains new value
    for (size_t i {0}; i < 3; ++i) {
        EXPECT_CALL(*mVisSubjectsObserverMockPtr, SubjectsChanged).Times(0);
        mVisIdentifier.HandleSubscription(kSubscriptionNofiticationJson);
    }
}

TEST_F(VisidentifierTest, SubscriptionNotificationNestedJsonReceivedAndObserverIsNotified)
{
    ExpectInitSucceeded();

    aos::StaticArray<aos::StaticString<aos::cSubjectIDLen>, 3> subjects;

    EXPECT_CALL(*mVisSubjectsObserverMockPtr, SubjectsChanged)
        .Times(1)
        .WillOnce(Invoke([&subjects](const auto& newSubjects) {
            subjects = newSubjects;

            return aos::ErrorEnum::eNone;
        }));

    const std::string kSubscriptionNofiticationJson
        = R"({"action":"subscription","subscriptionId":"1234-4321","value":{"Attribute.Aos.Subjects": [11,12,13]}, "timestamp": 0})";

    mVisIdentifier.HandleSubscription(kSubscriptionNofiticationJson);

    EXPECT_EQ(subjects.Size(), 3);

    // Observer is notified only if subsription json contains new value
    for (size_t i {0}; i < 3; ++i) {
        EXPECT_CALL(*mVisSubjectsObserverMockPtr, SubjectsChanged).Times(0);
        mVisIdentifier.HandleSubscription(kSubscriptionNofiticationJson);
    }
}

TEST_F(VisidentifierTest, SubscriptionNotificationReceivedUnknownSubscriptionId)
{
    ExpectInitSucceeded();

    EXPECT_CALL(*mVisSubjectsObserverMockPtr, SubjectsChanged).Times(0);

    mVisIdentifier.HandleSubscription(
        R"({"action":"subscription","subscriptionId":"unknown-subscriptionId","value":[11,12,13], "timestamp": 0})");
}

TEST_F(VisidentifierTest, SubscriptionNotificationReceivedInvalidPayload)
{
    ExpectInitSucceeded();

    EXPECT_CALL(*mVisSubjectsObserverMockPtr, SubjectsChanged).Times(0);

    ASSERT_NO_THROW(mVisIdentifier.HandleSubscription(R"({cActionTagName})"));
}

TEST_F(VisidentifierTest, SubscriptionNotificationValueExceedsMaxLimit)
{
    ExpectInitSucceeded();

    EXPECT_CALL(*mVisSubjectsObserverMockPtr, SubjectsChanged).Times(0);

    Poco::JSON::Object notification;

    notification.set("action", "subscription");
    notification.set("timestamp", 0);
    notification.set("subscriptionId", cTestSubscriptionId);
    notification.set("value", std::vector<std::string>(aos::cMaxSubjectIDSize + 1, "test"));

    std::ostringstream jsonStream;
    Poco::JSON::Stringifier::stringify(notification, jsonStream);

    ASSERT_NO_THROW(mVisIdentifier.HandleSubscription(jsonStream.str()));
}

TEST_F(VisidentifierTest, ReconnectOnFailSendFrame)
{
    EXPECT_CALL(mVisIdentifier, InitWSClient).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mWSClientItfMockPtr, Disconnect).Times(1);
    EXPECT_CALL(*mWSClientItfMockPtr, Connect).Times(2);

    EXPECT_CALL(*mWSClientItfMockPtr, WaitForEvent).WillOnce(Invoke([this]() { return mWSClientEvent.Wait(); }));

    EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(2);
    EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
        .Times(2)
        .WillOnce(Invoke([](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            throw WSException("mock");
        }))
        .WillOnce(Invoke([this](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            VISMessage message(VISActionEnum::eSubscribe);

            message.SetKeyValue("requestId", "id");
            message.SetKeyValue("subscriptionId", cTestSubscriptionId);
            message.SetKeyValue("path", "p");

            const auto str = message.ToString();

            return {str.cbegin(), str.cend()};
        }));

    const auto err = mVisIdentifier.Init(mConfig, mVisSubjectsObserverMockPtr);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    mVisIdentifier.WaitUntilConnected();
}

TEST_F(VisidentifierTest, GetSystemIDSucceds)
{
    ExpectInitSucceeded();

    const std::string cExpectedSystemId {"expectedSystemId"};

    EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
    EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
        .WillOnce(Invoke([&](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            Poco::JSON::Object response;

            response.set("action", "get");
            response.set("requestId", "requestId");
            response.set("timestamp", 0);
            response.set("value", cExpectedSystemId);

            std::ostringstream jsonStream;
            Poco::JSON::Stringifier::stringify(response, jsonStream);

            const auto str = jsonStream.str();

            return {str.cbegin(), str.cend()};
        }));

    aos::StaticString<aos::cSystemIDLen> systemId;
    aos::Error                           err;

    Tie(systemId, err) = mVisIdentifier.GetSystemID();
    EXPECT_TRUE(err.IsNone()) << err.Message();
    EXPECT_STREQ(systemId.CStr(), cExpectedSystemId.c_str());
}

TEST_F(VisidentifierTest, GetSystemIDNestedValueTagSucceds)
{
    ExpectInitSucceeded();

    const std::string cExpectedSystemId {"expectedSystemId"};

    EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
    EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
        .WillOnce(Invoke([&](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            Poco::JSON::Object valueTag;
            valueTag.set("Attribute.Vehicle.VehicleIdentification.VIN", cExpectedSystemId);

            Poco::JSON::Object response;

            response.set("action", "get");
            response.set("requestId", "requestId");
            response.set("timestamp", 0);
            response.set("value", valueTag);

            std::ostringstream jsonStream;
            Poco::JSON::Stringifier::stringify(response, jsonStream);

            const auto str = jsonStream.str();

            return {str.cbegin(), str.cend()};
        }));

    aos::StaticString<aos::cSystemIDLen> systemId;
    aos::Error                           err;

    Tie(systemId, err) = mVisIdentifier.GetSystemID();
    EXPECT_TRUE(err.IsNone()) << err.Message();
    EXPECT_STREQ(systemId.CStr(), cExpectedSystemId.c_str());
}

TEST_F(VisidentifierTest, GetSystemIDExceedsMaxSize)
{
    ExpectInitSucceeded();

    EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
    EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
        .WillOnce(Invoke([](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            Poco::JSON::Object response;

            response.set("action", "get");
            response.set("requestId", "requestId");
            response.set("timestamp", 0);
            response.set("value", std::string(aos::cSystemIDLen + 1, '1'));

            std::ostringstream jsonStream;
            Poco::JSON::Stringifier::stringify(response, jsonStream);

            const auto str = jsonStream.str();

            return {str.cbegin(), str.cend()};
        }));

    const auto err = mVisIdentifier.GetSystemID();
    EXPECT_TRUE(err.mError.Is(aos::ErrorEnum::eNoMemory)) << err.mError.Message();
}

TEST_F(VisidentifierTest, GetSystemIDRequestFailed)
{
    ExpectInitSucceeded();

    EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
    EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
        .WillOnce(Invoke([](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            throw WSException("mock");
        }));

    const auto err = mVisIdentifier.GetSystemID();
    EXPECT_TRUE(err.mError.Is(aos::ErrorEnum::eFailed)) << err.mError.Message();
}

TEST_F(VisidentifierTest, GetUnitModelExceedsMaxSize)
{
    ExpectInitSucceeded();

    EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
    EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
        .WillOnce(Invoke([](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            Poco::JSON::Object response;

            response.set("action", "get");
            response.set("requestId", "test-requestId");
            response.set("timestamp", 0);
            response.set("value", std::string(aos::cUnitModelLen + 1, '1'));

            std::ostringstream jsonStream;
            Poco::JSON::Stringifier::stringify(response, jsonStream);

            const auto str = jsonStream.str();

            return {str.cbegin(), str.cend()};
        }));

    const auto err = mVisIdentifier.GetUnitModel();
    EXPECT_TRUE(err.mError.Is(aos::ErrorEnum::eNoMemory)) << err.mError.Message();
}

TEST_F(VisidentifierTest, GetUnitModelRequestFailed)
{
    ExpectInitSucceeded();

    EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
    EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
        .WillOnce(Invoke([](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            throw WSException("mock");
        }));

    const auto err = mVisIdentifier.GetUnitModel();
    EXPECT_TRUE(err.mError.Is(aos::ErrorEnum::eFailed)) << err.mError.Message();
}

TEST_F(VisidentifierTest, GetSubjectsRequestFailed)
{
    ExpectInitSucceeded();

    EXPECT_CALL(*mWSClientItfMockPtr, GenerateRequestID).Times(1);
    EXPECT_CALL(*mWSClientItfMockPtr, SendRequest)
        .WillOnce(Invoke([](const std::string&, const WSClientItf::ByteArray&) -> WSClientItf::ByteArray {
            throw WSException("mock");
        }));

    aos::StaticArray<aos::StaticString<aos::cSubjectIDLen>, aos::cMaxSubjectIDSize> subjects;
    const auto err = mVisIdentifier.GetSubjects(subjects);
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed));
    EXPECT_TRUE(subjects.IsEmpty());
}
