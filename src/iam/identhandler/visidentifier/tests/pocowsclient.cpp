/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include <gmock/gmock.h>

#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tests/mocks/identprovidermock.hpp>

#include <common/logger/logger.hpp>
#include <iam/identhandler/visidentifier/pocowsclient.hpp>
#include <iam/identhandler/visidentifier/visidentifier.hpp>
#include <iam/identhandler/visidentifier/wsexception.hpp>

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

config::IdentifierConfig CreateConfigWithVisParams(const config::VISIdentifierModuleParams& params)
{
    Poco::JSON::Object::Ptr object = new Poco::JSON::Object();

    object->set("VISServer", params.mVISServer);
    object->set("caCertFile", params.mCaCertFile);
    object->set("webSocketTimeout", std::to_string(params.mWebSocketTimeout.Seconds()));

    config::IdentifierConfig cfg;
    cfg.mParams = object;

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
        mCryptoProvider = std::make_unique<crypto::DefaultCryptoProvider>();
        ASSERT_TRUE(mCryptoProvider->Init().IsNone()) << "Failed to initialize crypto provider";

        ASSERT_NO_THROW(mWsClientPtr
            = std::make_shared<PocoWSClient>(cConfig, *mCryptoProvider, WSClientItf::MessageHandlerFunc()));
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

    std::unique_ptr<crypto::DefaultCryptoProvider> mCryptoProvider;
    std::shared_ptr<PocoWSClient>                  mWsClientPtr;
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

TEST_F(PocoWSClientTests, VisidentifierGetSystemInfo)
{
    VISIdentifier visIdentifier;

    auto config = CreateConfigWithVisParams(cConfig);

    ASSERT_TRUE(visIdentifier.Init(config, *mCryptoProvider).IsNone());
    ASSERT_TRUE(visIdentifier.Start().IsNone());

    const std::string expectedSystemId {"test-system-id"};
    VISParams::Instance().Set("Attribute.Vehicle.VehicleIdentification.VIN", expectedSystemId);

    const std::string expectedUnitModel {"test-unit-model"};
    const std::string expectedVersion {"1.0.0"};
    VISParams::Instance().Set(
        "Attribute.Aos.UnitModel", std::string(expectedUnitModel).append(";").append(expectedVersion));

    auto systemInfo = std::make_unique<SystemInfo>();

    const auto err = visIdentifier.GetSystemInfo(*systemInfo);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    EXPECT_STREQ(systemInfo->mSystemID.CStr(), expectedSystemId.c_str());
    EXPECT_STREQ(systemInfo->mUnitModel.CStr(), expectedUnitModel.c_str());
    EXPECT_STREQ(systemInfo->mVersion.CStr(), expectedVersion.c_str());

    visIdentifier.Stop();
}

TEST_F(PocoWSClientTests, VisidentifierGetSubjects)
{
    VISIdentifier visIdentifier;

    auto config = CreateConfigWithVisParams(cConfig);

    ASSERT_TRUE(visIdentifier.Init(config, *mCryptoProvider).IsNone());
    ASSERT_TRUE(visIdentifier.Start().IsNone());

    const std::vector<std::string> testSubjects {"1", "2", "3"};
    VISParams::Instance().Set("Attribute.Aos.Subjects", testSubjects);
    StaticArray<StaticString<cIDLen>, 3> expectedSubjects;

    for (const auto& testSubject : testSubjects) {
        expectedSubjects.PushBack(testSubject.c_str());
    }

    StaticArray<StaticString<cIDLen>, 3> receivedSubjects;

    const auto err = visIdentifier.GetSubjects(receivedSubjects);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_EQ(receivedSubjects, expectedSubjects);

    visIdentifier.Stop();
}

} // namespace aos::iam::visidentifier
