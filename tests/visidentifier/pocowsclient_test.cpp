/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include <gmock/gmock.h>
#include <logger/logger.hpp>

#include "mocks/identhandlermock.hpp"
#include "visidentifier/pocowsclient.hpp"
#include "visidentifier/visidentifier.hpp"
#include "visidentifier/wsexception.hpp"
#include "visserver.hpp"

using namespace testing;

namespace aos::iam::visidentifier {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

const std::string cWebSocketURI("wss://localhost:4566");
const std::string cServerCertPath("certificates/ca.pem");
const std::string cServerKeyPath("certificates/ca.key");
const std::string cClientCertPath {"certificates/client.cer"};

config::Config CreateConfigWithVisParams(const config::VISIdentifierModuleParams& config)
{
    Poco::JSON::Object::Ptr object = new Poco::JSON::Object();

    object->set("VISServer", config.mVISServer);
    object->set("caCertFile", config.mCaCertFile);
    object->set("webSocketTimeout", std::to_string(config.mWebSocketTimeout.Seconds()));

    config::Config cfg;
    cfg.mIdentifier.mParams = object;

    return cfg;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class PocoWSClientTests : public Test {
protected:
    static const config::VISIdentifierModuleParams cConfig;

    void SetUp() override
    {
        ASSERT_NO_THROW(mWsClientPtr = std::make_shared<PocoWSClient>(cConfig, WSClientItf::MessageHandlerFunc()));
    }

    // This method is called before any test cases in the test suite
    static void SetUpTestSuite()
    {
        static common::logger::Logger mLogger;

        mLogger.SetBackend(common::logger::Logger::Backend::eStdIO);
        mLogger.SetLogLevel(LogLevelEnum::eDebug);
        mLogger.Init();

        Poco::Net::initializeSSL();

        VISWebSocketServer::Instance().Start(cServerKeyPath, cServerCertPath, cWebSocketURI);

        ASSERT_TRUE(VISWebSocketServer::Instance().TryWaitServiceStart());
    }

    static void TearDownTestSuite()
    {
        VISWebSocketServer::Instance().Stop();

        Poco::Net::uninitializeSSL();
    }

    std::shared_ptr<PocoWSClient> mWsClientPtr;
};

const config::VISIdentifierModuleParams PocoWSClientTests::cConfig {cWebSocketURI, cClientCertPath, 5 * Time::cSeconds};

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
        EXPECT_EQ(e.GetError(), ErrorEnum::eFailed);
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
        EXPECT_EQ(e.GetError(), ErrorEnum::eFailed);
    } catch (...) {
        FAIL() << "WSException expected";
    }

    SetUpTestSuite();
}

TEST_F(PocoWSClientTests, VisidentifierGetSystemID)
{
    VISIdentifier visIdentifier;

    config::Config config = CreateConfigWithVisParams(cConfig);

    iam::identhandler::SubjectsObserverMock observer;

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

    config::Config config = CreateConfigWithVisParams(cConfig);

    iam::identhandler::SubjectsObserverMock observer;

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

    config::Config config = CreateConfigWithVisParams(cConfig);

    iam::identhandler::SubjectsObserverMock observer;

    auto err = visIdentifier.Init(config, observer);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const std::vector<std::string> testSubjects {"1", "2", "3"};
    VISParams::Instance().Set("Attribute.Aos.Subjects", testSubjects);
    StaticArray<StaticString<cSubjectIDLen>, 3> expectedSubjects;

    for (const auto& testSubject : testSubjects) {
        expectedSubjects.PushBack(testSubject.c_str());
    }

    StaticArray<StaticString<cSubjectIDLen>, 3> receivedSubjects;

    err = visIdentifier.GetSubjects(receivedSubjects);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_EQ(receivedSubjects, expectedSubjects);
}

} // namespace aos::iam::visidentifier
