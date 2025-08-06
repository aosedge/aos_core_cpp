/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>

#include <Poco/JSON/Object.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tests/crypto/softhsmenv.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/iam/certhandler/certmodules/pkcs11/pkcs11.hpp>

#include <common/cloudprotocol/cloudmessage.hpp>
#include <common/cloudprotocol/servicediscovery.hpp>
#include <common/tests/stubs/storagestub.hpp>
#include <common/utils/cryptohelper.hpp>
#include <common/utils/pkcs11helper.hpp>

#include <cm/communication/communication.hpp>

#include <core/cm/tests/mocks/communicationmock.hpp>
#include <core/iam/tests/mocks/certprovidermock.hpp>

#include "stubs/certprovider.hpp"
#include "stubs/connectionsubscriber.hpp"
#include "stubs/httpserver.hpp"
#include "stubs/messagehandler.hpp"

using namespace testing;

namespace aos::cm::communication {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cConnectedEvent      = true;
constexpr auto cDisconnectedEvent   = false;
constexpr auto cDiscoveryServerPort = 3344;
constexpr auto cDiscoveryServerURL  = "https://localhost:3344";
constexpr auto cWebSocketServiceURL = "wss://localhost:3345";
constexpr auto cCloudServerPort     = 3345;

/**
 * Identity provider mock.
 */
class IdentityProviderMock : public IdentityProviderItf {
public:
    MOCK_METHOD(Error, GetSystemID, (String & systemID), (override));
};

std::string CreateDiscoveryResponse(const std::vector<std::string>& connectionInfo = {cWebSocketServiceURL})
{
    auto discoveryResponse = std::make_unique<aos::cloudprotocol::ServiceDiscoveryResponse>();

    for (const auto& info : connectionInfo) {
        discoveryResponse->mConnectionInfo.EmplaceBack(info.c_str());
    }

    auto responseJSON = Poco::makeShared<Poco::JSON::Object>();

    auto err = common::cloudprotocol::ToJSON(*discoveryResponse, *responseJSON);
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to convert discovery response to JSON");

    return common::utils::Stringify(*responseJSON);
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CMCommunicationTest : public Test {
public:
    static void SetUpTestSuite() { Poco::Net::initializeSSL(); }

    static void TearDownTestSuite() { Poco::Net::uninitializeSSL(); }

    void SetUp() override
    {
        tests::utils::InitLog();

        mConfig.mServiceDiscoveryURL = cDiscoveryServerURL;
        mConfig.mCrypt.mCACert       = CERTIFICATES_CM_DIR "/ca.cer";
        mConfig.mCertStorage         = "client";

        EXPECT_CALL(mIdentityProvider, GetSystemID)
            .WillRepeatedly(DoAll(SetArgReferee<0>(mSystemID), Return(ErrorEnum::eNone)));

        auto err = mCryptoProvider.Init();

        ASSERT_TRUE(err.IsNone()) << "Failed to initialize crypto provider: " << tests::utils::ErrorToStr(err);

        err = mSOFTHSMEnv.Init("", "certhandler-integration-tests", SOFTHSM_BASE_CM_DIR "/softhsm2.conf",
            SOFTHSM_BASE_CM_DIR "/tokens", SOFTHSM2_LIB);
        ASSERT_TRUE(err.IsNone()) << "Failed to initialize SOFTHSM environment: " << tests::utils::ErrorToStr(err);

        err = mCertLoader.Init(mCryptoProvider, mSOFTHSMEnv.GetManager());
        ASSERT_TRUE(err.IsNone()) << "Failed to initialize certificate loader: " << tests::utils::ErrorToStr(err);

        RegisterPKCS11Module(mConfig.mCertStorage.c_str());
        ASSERT_TRUE(mCertHandler.SetOwner(mConfig.mCertStorage.c_str(), cPIN).IsNone());

        RegisterPKCS11Module("server");

        ApplyCertificate(mConfig.mCertStorage.c_str(), mConfig.mCertStorage.c_str(),
            CERTIFICATES_CM_DIR "/client_int.key", CERTIFICATES_CM_DIR "/client_int.cer", 0x3333444, mClientInfo);
        ApplyCertificate("server", "localhost", CERTIFICATES_CM_DIR "/server_int.key",
            CERTIFICATES_CM_DIR "/server_int.cer", 0x3333333, mServerInfo);

        iam::certhandler::CertInfo certInfo;
        mCertHandler.GetCertificate("client", {}, {}, certInfo);
        auto [keyURI, errPkcs] = common::utils::CreatePKCS11URL(certInfo.mKeyURL);
        EXPECT_EQ(errPkcs, ErrorEnum::eNone);

        auto [certPEM, err2] = common::utils::LoadPEMCertificates(certInfo.mCertURL, mCertLoader, mCryptoProvider);
        EXPECT_EQ(err2, ErrorEnum::eNone);

        StartHTTPServer();
    }

    void TearDown() override { StopHTTPServer(); }

    void StartHTTPServer()
    {
        ASSERT_NO_THROW(mDiscoverySendMessages.Push(CreateDiscoveryResponse()));

        mCloudServer.emplace(
            cCloudServerPort, cServerKey, cServerCert, cCA, mCloudReceivedMessages, mCloudSendMessageQueue);
        mCloudServer->Start();

        mDiscoveryServer.emplace(
            cDiscoveryServerPort, cServerKey, cServerCert, cCA, mDiscoveryReceivedMessages, mDiscoverySendMessages);
        mDiscoveryServer->Start();

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void StopHTTPServer()
    {
        auto stopHTTP = std::async(std::launch::async, [this] {
            if (mDiscoveryServer) {
                mDiscoveryServer->Stop();
            }
        });

        auto stopCloud = std::async(std::launch::async, [this] {
            if (mCloudServer) {
                mCloudServer->Stop();
            }
        });
    }

    void SubscribeAndWaitConnected()
    {
        auto err = mCommunication.Init(
            mConfig, mMessageHandler, mIdentityProvider, mCertProvider, mCertLoader, mCryptoProvider);
        ASSERT_TRUE(err.IsNone()) << "Failed to initialize communication: " << tests::utils::ErrorToStr(err);

        err = mCommunication.Subscribe(mConnectionSubscriber);
        ASSERT_TRUE(err.IsNone()) << "Failed to subscribe to connection events: " << tests::utils::ErrorToStr(err);

        err = mCommunication.Start();
        ASSERT_TRUE(err.IsNone()) << "Failed to start communication: " << tests::utils::ErrorToStr(err);

        err = mConnectionSubscriber.WaitEvent(cConnectedEvent);
        EXPECT_TRUE(err.IsNone()) << "Failed to wait for connection established event: "
                                  << tests::utils::ErrorToStr(err);
    }

    iam::certhandler::PKCS11ModuleConfig GetPKCS11ModuleConfig()
    {
        iam::certhandler::PKCS11ModuleConfig config;

        config.mLibrary         = SOFTHSM2_LIB;
        config.mSlotID          = mSOFTHSMEnv.GetSlotID();
        config.mUserPINPath     = CERTIFICATES_CM_DIR "/pin.txt";
        config.mModulePathInURL = true;

        return config;
    }

    iam::certhandler::ModuleConfig GetCertModuleConfig(crypto::KeyType keyType)
    {
        iam::certhandler::ModuleConfig config;

        config.mKeyType         = keyType;
        config.mMaxCertificates = 2;
        config.mExtendedKeyUsage.EmplaceBack(iam::certhandler::ExtendedKeyUsageEnum::eClientAuth);
        config.mAlternativeNames.EmplaceBack("epam.com");
        config.mAlternativeNames.EmplaceBack("www.epam.com");
        config.mSkipValidation = false;

        return config;
    }

    void RegisterPKCS11Module(const String& name, crypto::KeyType keyType = crypto::KeyTypeEnum::eRSA)
    {
        ASSERT_TRUE(mPKCS11Modules.EmplaceBack().IsNone());
        ASSERT_TRUE(mCertModules.EmplaceBack().IsNone());
        auto& pkcs11Module = mPKCS11Modules.Back();
        auto& certModule   = mCertModules.Back();
        ASSERT_TRUE(
            pkcs11Module.Init(name, GetPKCS11ModuleConfig(), mSOFTHSMEnv.GetManager(), mCryptoProvider).IsNone());
        ASSERT_TRUE(
            certModule.Init(name, GetCertModuleConfig(keyType), mCryptoProvider, pkcs11Module, mStorage).IsNone());
        ASSERT_TRUE(mCertHandler.RegisterModule(certModule).IsNone());
    }

    void ApplyCertificate(const String& certType, const String& subject, const String& intermKeyPath,
        const String& intermCertPath, uint64_t serial, iam::certhandler::CertInfo& certInfo)
    {
        StaticString<crypto::cCSRPEMLen> csr;
        ASSERT_TRUE(mCertHandler.CreateKey(certType, subject, cPIN, csr).IsNone());

        // create certificate from CSR, CA priv key, CA cert
        StaticString<crypto::cPrivKeyPEMLen> intermKey;
        ASSERT_TRUE(fs::ReadFileToString(intermKeyPath, intermKey).IsNone());

        StaticString<crypto::cCertPEMLen> intermCert;
        ASSERT_TRUE(fs::ReadFileToString(intermCertPath, intermCert).IsNone());

        auto serialArr = Array<uint8_t>(reinterpret_cast<uint8_t*>(&serial), sizeof(serial));
        StaticString<crypto::cCertPEMLen> clientCertChain;

        ASSERT_TRUE(mCryptoProvider.CreateClientCert(csr, intermKey, intermCert, serialArr, clientCertChain).IsNone());

        // add intermediate cert to the chain
        clientCertChain.Append(intermCert);

        // add CA certificate to the chain
        StaticString<crypto::cCertPEMLen> caCert;

        ASSERT_TRUE(fs::ReadFileToString(CERTIFICATES_CM_DIR "/ca.cer", caCert).IsNone());
        clientCertChain.Append(caCert);

        // apply client certificate
        auto err = mCertHandler.ApplyCertificate(certType, clientCertChain, certInfo);
        ASSERT_TRUE(err.IsNone()) << "Failed to apply certificate: " << tests::utils::ErrorToStr(err);
        EXPECT_EQ(certInfo.mSerial, serialArr);
    }

protected:
    static constexpr auto cMaxModulesCount = 3;
    static constexpr auto cPIN             = "admin";
    static constexpr auto cLabel           = "cm-communication-test-slot";
    static constexpr auto cServerKey       = CERTIFICATES_CM_DIR "/server_int.key";
    static constexpr auto cServerCert      = CERTIFICATES_CM_DIR "/server_int.cer";
    static constexpr auto cCA              = CERTIFICATES_CM_DIR "/ca.cer";

    MessageQueue mDiscoveryReceivedMessages;
    MessageQueue mDiscoverySendMessages;

    MessageQueue mCloudReceivedMessages;
    MessageQueue mCloudSendMessageQueue;

    StaticString<cSystemIDLen>         mSystemID = "test_system_id";
    config::Config                     mConfig;
    MessageHandlerStub                 mMessageHandler;
    ConnectionSubscriberStub           mConnectionSubscriber;
    IdentityProviderMock               mIdentityProvider;
    std::optional<HTTPServer>          mDiscoveryServer;
    std::optional<HTTPServer>          mCloudServer;
    iam::certhandler::CertHandler      mCertHandler;
    iam::certhandler::CertInfo         mClientInfo;
    iam::certhandler::CertInfo         mServerInfo;
    crypto::DefaultCryptoProvider      mCryptoProvider;
    iam::certhandler::CertProviderStub mCertProvider {mCertHandler};
    crypto::CertLoader                 mCertLoader;
    Communication                      mCommunication;

    test::SoftHSMEnv                                              mSOFTHSMEnv;
    iam::certhandler::StorageStub                                 mStorage;
    StaticArray<iam::certhandler::PKCS11Module, cMaxModulesCount> mPKCS11Modules;
    StaticArray<iam::certhandler::CertModule, cMaxModulesCount>   mCertModules;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CMCommunicationTest, ConnectionSucceedsOnValidURLInDiscoveryResponse)
{
    mDiscoverySendMessages.Clear();

    ASSERT_NO_THROW(mDiscoverySendMessages.Push(
        CreateDiscoveryResponse({"not a valid URL", "https://localhost:22", cDiscoveryServerURL})));

    SubscribeAndWaitConnected();

    auto err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop communication: " << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, Reconnect)
{
    SubscribeAndWaitConnected();

    StopHTTPServer();

    auto err = mConnectionSubscriber.WaitEvent(cDisconnectedEvent, std::chrono::seconds(15));
    EXPECT_TRUE(err.IsNone()) << "Failed to wait for connection lost event: " << tests::utils::ErrorToStr(err);

    StartHTTPServer();

    err = mConnectionSubscriber.WaitEvent(cConnectedEvent);
    EXPECT_TRUE(err.IsNone()) << "Failed to wait for connection established event: " << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop communication: " << tests::utils::ErrorToStr(err);

    err = mConnectionSubscriber.WaitEvent(cDisconnectedEvent);
    EXPECT_TRUE(err.IsNone()) << "Failed to wait for connection lost event: " << tests::utils::ErrorToStr(err);

    mCommunication.Unsubscribe(mConnectionSubscriber);
}

TEST_F(CMCommunicationTest, SubscribeUnsubscribe)
{
    SubscribeAndWaitConnected();

    auto err = mCommunication.Subscribe(mConnectionSubscriber);
    ASSERT_TRUE(err.Is(ErrorEnum::eAlreadyExist))
        << "Expected error when subscribing to connection events again: " << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop communication: " << tests::utils::ErrorToStr(err);

    err = mConnectionSubscriber.WaitEvent(cDisconnectedEvent);
    EXPECT_TRUE(err.IsNone()) << "Failed to wait for connection lost event: " << tests::utils::ErrorToStr(err);

    mCommunication.Unsubscribe(mConnectionSubscriber);

    mCommunication.Unsubscribe(mConnectionSubscriber);
}

TEST_F(CMCommunicationTest, SendInstanceNewState)
{
    SubscribeAndWaitConnected();

    auto body = std::make_unique<cloudprotocol::MessageVariant>();

    auto newState       = std::make_unique<cloudprotocol::NewState>(InstanceIdent {"service", "subject", 0});
    newState->mChecksum = "test_checksum";
    newState->mState    = "test_state";

    body->SetValue(*newState);

    auto err = mCommunication.SendMessage(*body);
    EXPECT_TRUE(err.IsNone()) << "Failed to send instance new state: " << tests::utils::ErrorToStr(err);

    auto sentStr = mCloudReceivedMessages.Pop();
    EXPECT_TRUE(sentStr.has_value()) << "No message received";

    auto sentOb = std::make_unique<aos::cloudprotocol::CloudMessage>();

    err = common::cloudprotocol::FromJSON(*sentStr, *sentOb);
    EXPECT_TRUE(err.IsNone()) << "Failed to parse received message: " << tests::utils::ErrorToStr(err);

    auto expected     = std::make_unique<aos::cloudprotocol::CloudMessage>();
    expected->mHeader = {aos::cloudprotocol::cProtocolVersion, mSystemID};
    expected->mData.SetValue(*newState);

    EXPECT_EQ(*sentOb, *expected) << "Sent message does not match expected message";

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop communication: " << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendInstanceStateRequest)
{
    SubscribeAndWaitConnected();

    auto body = std::make_unique<cloudprotocol::MessageVariant>();

    auto stateRequest = std::make_unique<cloudprotocol::StateRequest>(InstanceIdent {"service", "subject", 0});

    body->SetValue(*stateRequest);

    auto err = mCommunication.SendMessage(*body);
    EXPECT_TRUE(err.IsNone()) << "Failed to send message: " << tests::utils::ErrorToStr(err);

    auto sentStr = mCloudReceivedMessages.Pop();
    EXPECT_TRUE(sentStr.has_value()) << "No message received";

    auto sentOb = std::make_unique<aos::cloudprotocol::CloudMessage>();

    err = common::cloudprotocol::FromJSON(*sentStr, *sentOb);
    EXPECT_TRUE(err.IsNone()) << "Failed to parse received message: " << tests::utils::ErrorToStr(err);

    auto expected     = std::make_unique<aos::cloudprotocol::CloudMessage>();
    expected->mHeader = {aos::cloudprotocol::cProtocolVersion, mSystemID};
    expected->mData.SetValue(*stateRequest);

    EXPECT_EQ(*sentOb, *expected) << "Sent message does not match expected message";

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop communication: " << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveMessage)
{
    SubscribeAndWaitConnected();

    auto updateState       = std::make_unique<cloudprotocol::UpdateState>(InstanceIdent {"service", "subject", 0});
    updateState->mChecksum = "test_checksum";
    updateState->mState    = "test_state";

    auto msg               = std::make_unique<aos::cloudprotocol::CloudMessage>();
    msg->mHeader.mSystemID = mSystemID;
    msg->mHeader.mVersion  = cloudprotocol::cProtocolVersion;

    msg->mData.SetValue(*updateState);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = common::cloudprotocol::ToJSON(*msg, *json);
    EXPECT_TRUE(err.IsNone()) << "Failed to convert message to JSON: " << tests::utils::ErrorToStr(err);

    mCloudSendMessageQueue.Push(common::utils::Stringify(json));

    err = mMessageHandler.WaitMessageReceived(msg->mData, std::chrono::seconds(5));
    EXPECT_TRUE(err.IsNone()) << "Failed to receive message: " << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop communication: " << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveMessageWithDifferentSystemID)
{
    SubscribeAndWaitConnected();

    auto updateState       = std::make_unique<cloudprotocol::UpdateState>(InstanceIdent {"service", "subject", 0});
    updateState->mChecksum = "test_checksum";
    updateState->mState    = "test_state";

    auto CloudMessage               = std::make_unique<aos::cloudprotocol::CloudMessage>();
    CloudMessage->mHeader.mSystemID = mSystemID.CStr();
    CloudMessage->mHeader.mSystemID.Append("_different");
    CloudMessage->mHeader.mVersion = cloudprotocol::cProtocolVersion;

    CloudMessage->mData.SetValue(*updateState);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = common::cloudprotocol::ToJSON(*CloudMessage, *json);
    EXPECT_TRUE(err.IsNone()) << "Failed to convert message to JSON: " << tests::utils::ErrorToStr(err);

    mCloudSendMessageQueue.Push(common::utils::Stringify(json));

    err = mMessageHandler.WaitMessageReceived(CloudMessage->mData, std::chrono::seconds(1));
    EXPECT_TRUE(err.Is(ErrorEnum::eTimeout))
        << "Expected timeout when receiving message with different system ID, but got: "
        << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop communication: " << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveMessageWithDifferentProtocolVersion)
{
    SubscribeAndWaitConnected();

    auto updateState       = std::make_unique<cloudprotocol::UpdateState>(InstanceIdent {"service", "subject", 0});
    updateState->mChecksum = "test_checksum";
    updateState->mState    = "test_state";

    auto CloudMessage               = std::make_unique<aos::cloudprotocol::CloudMessage>();
    CloudMessage->mHeader.mSystemID = mSystemID.CStr();
    CloudMessage->mHeader.mVersion  = cloudprotocol::cProtocolVersion + 1;

    CloudMessage->mData.SetValue(*updateState);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = common::cloudprotocol::ToJSON(*CloudMessage, *json);
    EXPECT_TRUE(err.IsNone()) << "Failed to convert message to JSON: " << tests::utils::ErrorToStr(err);

    mCloudSendMessageQueue.Push(common::utils::Stringify(json));

    err = mMessageHandler.WaitMessageReceived(CloudMessage->mData, std::chrono::seconds(1));
    EXPECT_TRUE(err.Is(ErrorEnum::eTimeout))
        << "Expected timeout when receiving message with different protocol version, but got: "
        << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop communication: " << tests::utils::ErrorToStr(err);
}

} // namespace aos::cm::communication
