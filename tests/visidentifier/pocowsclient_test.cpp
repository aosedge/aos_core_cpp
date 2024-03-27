/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include <gmock/gmock.h>

#include "logger/logger.hpp"
#include "mocks/vissubjectsobservermock.hpp"
#include "visidentifier/pocowsclient.hpp"
#include "visidentifier/visidentifier.hpp"
#include "visidentifier/wsexception.hpp"
#include "visserver.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static const std::string cWebSockerURI("wss://localhost:4566");
static const std::string cServerCertPath("certificates/ca.pem");
static const std::string cServerKeyPath("certificates/ca.key");
static const std::string cClientCertPath {"certificates/client.cer"};

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class PocoWSClientTests : public Test {
protected:
    static const VISConfig cConfig;

    void SetUp() override
    {
        ASSERT_NO_THROW(mWsClientPtr = std::make_shared<PocoWSClient>(cConfig, WSClientItf::MessageHandlerFunc()));
    }

    // This method is called before any test cases in the test suite
    static void SetUpTestSuite()
    {
        static Logger mLogger;

        mLogger.SetBackend(Logger::Backend::eStdIO);
        mLogger.SetLogLevel(aos::LogLevelEnum::eDebug);
        mLogger.Init();

        Poco::Net::initializeSSL();

        VISWebSocketServer::Instance().Start(cServerKeyPath, cServerCertPath, cWebSockerURI);

        ASSERT_TRUE(VISWebSocketServer::Instance().TryWaitServiceStart());
    }

    static void TearDownTestSuite()
    {
        VISWebSocketServer::Instance().Stop();

        Poco::Net::uninitializeSSL();
    }

    std::shared_ptr<PocoWSClient> mWsClientPtr;
};

const VISConfig PocoWSClientTests::cConfig {cWebSockerURI, cClientCertPath, std::chrono::seconds(5)};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PocoWSClientTests, Connect)
{
    ASSERT_NO_THROW(mWsClientPtr->Connect());
    ASSERT_NO_THROW(mWsClientPtr->Connect());
}

TEST_F(PocoWSClientTests, Close)
{
    ASSERT_NO_THROW(mWsClientPtr->Connect());
    ASSERT_NO_THROW(mWsClientPtr->Close());
    ASSERT_NO_THROW(mWsClientPtr->Close());
}

TEST_F(PocoWSClientTests, Disconnect)
{
    ASSERT_NO_THROW(mWsClientPtr->Disconnect());

    ASSERT_NO_THROW(mWsClientPtr->Connect());
    ASSERT_NO_THROW(mWsClientPtr->Disconnect());
}

TEST_F(PocoWSClientTests, GenerateRequestID)
{
    std::string requestId;
    ASSERT_NO_THROW(requestId = mWsClientPtr->GenerateRequestID());
    ASSERT_FALSE(requestId.empty());
}

TEST_F(PocoWSClientTests, AsyncSendMessageSucceeds)
{
    const WSClientItf::ByteArray message = {'t', 'e', 's', 't'};

    ASSERT_NO_THROW(mWsClientPtr->Connect());
    ASSERT_NO_THROW(mWsClientPtr->AsyncSendMessage(message));
}

TEST_F(PocoWSClientTests, AsyncSendMessageNotConnected)
{
    try {
        const WSClientItf::ByteArray message = {'t', 'e', 's', 't'};

        mWsClientPtr->AsyncSendMessage(message);
    } catch (const WSException& e) {
        EXPECT_EQ(e.GetError(), aos::ErrorEnum::eFailed);
    } catch (...) {
        FAIL() << "WSException expected";
    }
}

TEST_F(PocoWSClientTests, AsyncSendMessageFails)
{
    mWsClientPtr->Connect();

    TearDownTestSuite();

    try {
        const WSClientItf::ByteArray message = {'t', 'e', 's', 't'};

        mWsClientPtr->AsyncSendMessage(message);
    } catch (const WSException& e) {
        EXPECT_EQ(e.GetError(), aos::ErrorEnum::eFailed);
    } catch (...) {
        FAIL() << "WSException expected";
    }

    SetUpTestSuite();
}

TEST_F(PocoWSClientTests, VisidentifierGetSystemID)
{
    VISIdentifier visIdentifier;

    Config config;
    config.mIdentifier.mParams = cConfig.ToJSON();

    VISSubjectsObserverMock observer;

    auto err = visIdentifier.Init(config, observer);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const std::string expectedSystemId {"test-system-id"};
    VISParams::Instance().Set("Attribute.Vehicle.VehicleIdentification.VIN", expectedSystemId);

    const auto systemId = visIdentifier.GetSystemID();
    EXPECT_TRUE(systemId.mError.IsNone()) << systemId.mError.Message();
    EXPECT_STREQ(systemId.mValue.CStr(), expectedSystemId.c_str());
}

TEST_F(PocoWSClientTests, VisidentifierGetUnitModel)
{
    VISIdentifier visIdentifier;

    Config config;
    config.mIdentifier.mParams = cConfig.ToJSON();

    VISSubjectsObserverMock observer;

    auto err = visIdentifier.Init(config, observer);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const std::string expectedUnitModel {"test-unit-model"};
    VISParams::Instance().Set("Attribute.Aos.UnitModel", expectedUnitModel);

    const auto unitModel = visIdentifier.GetUnitModel();
    EXPECT_TRUE(unitModel.mError.IsNone()) << unitModel.mError.Message();
    EXPECT_STREQ(unitModel.mValue.CStr(), expectedUnitModel.c_str());
}

TEST_F(PocoWSClientTests, VisidentifierGetSubjects)
{
    VISIdentifier visIdentifier;

    Config config;
    config.mIdentifier.mParams = cConfig.ToJSON();

    VISSubjectsObserverMock observer;

    auto err = visIdentifier.Init(config, observer);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const std::vector<std::string> testSubjects {"1", "2", "3"};
    VISParams::Instance().Set("Attribute.Aos.Subjects", testSubjects);
    aos::StaticArray<aos::StaticString<aos::cSubjectIDLen>, 3> expectedSubjects;

    for (const auto& testSubject : testSubjects) {
        expectedSubjects.PushBack(testSubject.c_str());
    }

    aos::StaticArray<aos::StaticString<aos::cSubjectIDLen>, 3> receivedSubjects;

    err = visIdentifier.GetSubjects(receivedSubjects);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_EQ(receivedSubjects, expectedSubjects);
}
