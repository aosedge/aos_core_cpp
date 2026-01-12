/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <future>
#include <regex>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/crypto/certloader.hpp>
#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tests/crypto/softhsmenv.hpp>
#include <core/common/tests/mocks/certhandlermock.hpp>
#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/mocks/identprovidermock.hpp>
#include <core/common/tests/mocks/provisioningmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/common/tools/fs.hpp>
#include <core/common/types/state.hpp>
#include <core/iam/certhandler/certmodules/pkcs11/pkcs11.hpp>

#include <common/tests/stubs/storagestub.hpp>
#include <common/utils/cryptohelper.hpp>
#include <common/utils/pkcs11helper.hpp>

#include <cm/communication/cloudprotocol/servicediscovery.hpp>
#include <cm/communication/communication.hpp>

#include "mocks/launchermock.hpp"
#include "mocks/smcontrollermock.hpp"
#include "mocks/storagestatemock.hpp"
#include "mocks/updatemanagermock.hpp"
#include "stubs/certprovider.hpp"
#include "stubs/connectionsubscriber.hpp"
#include "stubs/httpserver.hpp"

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
constexpr auto cCertificate         = "online";

/***********************************************************************************************************************
 * Mocks
 **********************************************************************************************************************/

class UUIDItfStub : public crypto::UUIDItf {
public:
    void SetUUID(const uuid::UUID& uuid)
    {
        std::lock_guard lock {mMutex};

        mUUIDs.push_back(uuid);
    }

    void SetUUID(const String& uuidStr)
    {
        std::lock_guard lock {mMutex};

        auto result = uuid::StringToUUID(uuidStr);
        AOS_ERROR_CHECK_AND_THROW(result.mError, "Failed to convert string to UUID");

        mUUIDs.push_back(result.mValue);
    }

    RetWithError<uuid::UUID> CreateUUIDv4()
    {
        std::lock_guard lock {mMutex};

        if (mUUIDs.empty()) {
            return RetWithError<uuid::UUID>({}, ErrorEnum::eFailed);
        }

        if (mUUIDs.size() == 1) {
            return RetWithError<uuid::UUID>(mUUIDs[0], ErrorEnum::eNone);
        }

        const auto uuid = mUUIDs[0];
        mUUIDs.erase(mUUIDs.begin());

        return RetWithError<uuid::UUID>(uuid, ErrorEnum::eNone);
    }

    RetWithError<uuid::UUID> CreateUUIDv5(const uuid::UUID&, const Array<uint8_t>&)
    {
        return RetWithError<uuid::UUID>({}, ErrorEnum::eNotSupported);
    }

private:
    std::mutex              mMutex;
    std::vector<uuid::UUID> mUUIDs;
};

std::string CreateDiscoveryResponse(const std::vector<std::string>& connectionInfo = {cWebSocketServiceURL})
{
    auto responseJSON = Poco::makeShared<Poco::JSON::Object>();

    responseJSON->set("nextRequestDelay", 30);

    auto connectionInfoArray = Poco::makeShared<Poco::JSON::Array>();
    for (const auto& info : connectionInfo) {
        connectionInfoArray->add(info);
    }

    responseJSON->set("connectionInfo", connectionInfoArray);

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

        mConfig.mServiceDiscoveryURL      = cDiscoveryServerURL;
        mConfig.mCACert                   = CERTIFICATES_CM_DIR "/ca.cer";
        mConfig.mCloudResponseWaitTimeout = Time::cSeconds * 5;

        EXPECT_CALL(mIdentProviderMock, GetSystemInfo).WillRepeatedly(Invoke([this](SystemInfo& info) {
            info.mSystemID = mSystemID;
            return ErrorEnum::eNone;
        }));

        auto err = mCryptoProvider.Init();

        ASSERT_TRUE(err.IsNone()) << "Failed to initialize crypto provider: " << tests::utils::ErrorToStr(err);

        err = mSOFTHSMEnv.Init("", "certhandler-integration-tests", SOFTHSM_BASE_CM_DIR "/softhsm2.conf",
            SOFTHSM_BASE_CM_DIR "/tokens", SOFTHSM2_LIB);
        ASSERT_TRUE(err.IsNone()) << "Failed to initialize SOFTHSM environment: " << tests::utils::ErrorToStr(err);

        err = mCertLoader.Init(mCryptoProvider, mSOFTHSMEnv.GetManager());
        ASSERT_TRUE(err.IsNone()) << "Failed to initialize certificate loader: " << tests::utils::ErrorToStr(err);

        RegisterPKCS11Module(cCertificate);
        ASSERT_TRUE(mCertHandler.SetOwner(cCertificate, cPIN).IsNone());

        RegisterPKCS11Module("server");

        ApplyCertificate(cCertificate, cCertificate, CERTIFICATES_CM_DIR "/client_int.key",
            CERTIFICATES_CM_DIR "/client_int.cer", 0x3333444, mClientInfo);
        ApplyCertificate("server", "localhost", CERTIFICATES_CM_DIR "/server_int.key",
            CERTIFICATES_CM_DIR "/server_int.cer", 0x3333333, mServerInfo);

        CertInfo certInfo;
        mCertHandler.GetCert(cCertificate, {}, {}, certInfo);
        auto [keyURI, errPkcs] = common::utils::CreatePKCS11URL(certInfo.mKeyURL);
        EXPECT_EQ(errPkcs, ErrorEnum::eNone);

        auto [certPEM, err2] = common::utils::LoadPEMCertificates(certInfo.mCertURL, mCertLoader, mCryptoProvider);
        EXPECT_EQ(err2, ErrorEnum::eNone);

        err = mCryptoHelper.Init(mCertProviderStub, mCryptoProvider, mCertLoader, mConfig.mServiceDiscoveryURL.c_str(),
            mConfig.mCACert.c_str());
        ASSERT_TRUE(err.IsNone()) << "Failed to initialize crypto helper: " << tests::utils::ErrorToStr(err);

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
        EXPECT_CALL(mCurrentNodeInfoProviderMock, GetCurrentNodeInfo).WillRepeatedly(Invoke([this](NodeInfo& info) {
            info.mNodeID = mNodeID;

            return ErrorEnum::eNone;
        }));

        auto err = mCommunication.Init(mConfig, mCurrentNodeInfoProviderMock, mIdentProviderMock, mCertProviderStub,
            mCertLoader, mCryptoProvider, mCryptoHelper, mUUIDProvider, mUpdateManagerMock, mStateHandlerMock,
            mLogProviderMock, mEnvVarHandlerMock, mCertHandlerMock, mProvisioningMock);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        err = mCommunication.SubscribeListener(mConnectionSubscriberStub);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        err = mCommunication.Start();
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        err = mConnectionSubscriberStub.WaitEvent(cConnectedEvent);
        EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
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
        const String& intermCertPath, uint64_t serial, CertInfo& certInfo)
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

    StaticString<cIDLen>                   mSystemID = "test_system_id";
    StaticString<cIDLen>                   mNodeID   = "node0";
    config::Config                         mConfig;
    ConnectionSubscriberStub               mConnectionSubscriberStub;
    iamclient::CurrentNodeInfoProviderMock mCurrentNodeInfoProviderMock;
    iamclient::IdentProviderMock           mIdentProviderMock;
    updatemanager::UpdateManagerMock       mUpdateManagerMock;
    storagestate::StateHandlerMock         mStateHandlerMock;
    smcontroller::LogProviderMock          mLogProviderMock;
    launcher::EnvVarHandlerMock            mEnvVarHandlerMock;
    iamclient::CertHandlerMock             mCertHandlerMock;
    iamclient::ProvisioningMock            mProvisioningMock;

    std::optional<HTTPServer>     mDiscoveryServer;
    std::optional<HTTPServer>     mCloudServer;
    iam::certhandler::CertHandler mCertHandler;
    CertInfo                      mClientInfo;
    CertInfo                      mServerInfo;
    crypto::DefaultCryptoProvider mCryptoProvider;
    crypto::CryptoHelper          mCryptoHelper;
    UUIDItfStub                   mUUIDProvider;
    iamclient::CertProviderStub   mCertProviderStub {mCertHandler};
    crypto::CertLoader            mCertLoader;
    Communication                 mCommunication;

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
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, Reconnect)
{
    SubscribeAndWaitConnected();

    StopHTTPServer();

    auto err = mConnectionSubscriberStub.WaitEvent(cDisconnectedEvent, std::chrono::seconds(15));
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    StartHTTPServer();

    err = mConnectionSubscriberStub.WaitEvent(cConnectedEvent);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mConnectionSubscriberStub.WaitEvent(cDisconnectedEvent);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mCommunication.UnsubscribeListener(mConnectionSubscriberStub);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SubscribeUnsubscribe)
{
    SubscribeAndWaitConnected();

    auto err = mCommunication.SubscribeListener(mConnectionSubscriberStub);
    ASSERT_TRUE(err.Is(ErrorEnum::eAlreadyExist)) << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mConnectionSubscriberStub.WaitEvent(cDisconnectedEvent);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mCommunication.UnsubscribeListener(mConnectionSubscriberStub);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mCommunication.UnsubscribeListener(mConnectionSubscriberStub);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, MessageIsRecentIfAckNotReceived)
{
    mConfig.mCloudResponseWaitTimeout = Time::cMilliseconds * 500;

    const auto cExpectedMessage
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"alerts","correlationId":"id","items":\[\]\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("fb6e8461-2601-4f9a-8957-7ab4e52f304c");

    // cppcheck-suppress templateRecursion
    auto alerts            = std::make_unique<Alerts>();
    alerts->mCorrelationID = "id";

    auto err = mCommunication.SendAlerts(*alerts);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    for (size_t i = 0; i < 4; ++i) {
        const auto message = mCloudReceivedMessages.Pop().value_or("");
        EXPECT_TRUE(std::regex_match(message, cExpectedMessage))
            << "Message does not match expected regex: " << message;
    }

    const auto message
        = mCloudReceivedMessages.Pop(std::chrono::milliseconds(mConfig.mCloudResponseWaitTimeout.Milliseconds()));
    EXPECT_FALSE(message.has_value()) << "No more messages expected, but received: " << message.value();

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendAlerts)
{
    const auto cExpectedMessage
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"alerts","correlationId":"id","items":\[\]\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("fb6e8461-2601-4f9a-8957-7ab4e52f304c");

    // cppcheck-suppress templateRecursion
    auto alerts            = std::make_unique<Alerts>();
    alerts->mCorrelationID = "id";

    auto err = mCommunication.SendAlerts(*alerts);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(std::regex_match(mCloudReceivedMessages.Pop().value_or(""), cExpectedMessage));

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendOverrideEnvsStatuses)
{
    const auto cExpectedMessage
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"overrideEnvVarsStatus","statuses":\[\]\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("fb6e8461-2601-4f9a-8957-7ab4e52f304c");

    auto statuses = std::make_unique<OverrideEnvVarsStatuses>();

    auto err = mCommunication.SendOverrideEnvsStatuses(*statuses);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(std::regex_match(mCloudReceivedMessages.Pop().value_or(""), cExpectedMessage));

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, GetBlobsInfosTimeout)
{
    mConfig.mCloudResponseWaitTimeout = Time::cMilliseconds;

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("fb6e8461-2601-4f9a-8957-7ab4e52f304c");

    StaticArray<StaticString<oci::cDigestLen>, 1> digests;
    digests.EmplaceBack("sha256:3c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b");

    auto blobsInfo = std::make_unique<StaticArray<BlobInfo, 1>>();

    auto err = mCommunication.GetBlobsInfos(digests, *blobsInfo);
    EXPECT_TRUE(err.Is(ErrorEnum::eTimeout)) << tests::utils::ErrorToStr(err);

    // make sure get blobs info message is not sent one more time after timeout

    auto result
        = mCloudSendMessageQueue.Pop(std::chrono::milliseconds(2 * mConfig.mCloudResponseWaitTimeout.Milliseconds()));

    EXPECT_FALSE(result.has_value()) << "No more messages expected, but received: " << result.value();

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, GetBlobsInfos)
{
    constexpr auto cRequestCorrelationID = "2a05b9cc-32fb-41b6-a099-0fca3bb39ce2";
    constexpr auto cRequestTxnID         = "fb6e8461-2601-4f9a-8957-7ab4e52f304c";
    constexpr auto cDigest               = "sha256:3c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b";
    const auto     cExpectedMessage      = std::regex(
        R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                 R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                 R"("data":\{"messageType":"requestBlobUrls","correlationId":"2a05b9cc-32fb-41b6-a099-0fca3bb39ce2",)"
                 R"("digests":\["sha256:3c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b"\]\}\}$)");
    constexpr auto cCloudAck = R"({
        "header": {
            "version": 7,
            "systemId": "test_system_id",
            "txn": "fb6e8461-2601-4f9a-8957-7ab4e52f304c"
        },
        "data": {
            "messageType": "ack"
        }
    })";
    constexpr auto cResponse = R"({
        "header": {
            "version": 7,
            "systemId": "test_system_id",
            "txn": "f2df3016-de31-46e6-9ce8-9cde4ed7849f"
        },
        "data": {
            "messageType": "blobUrls",
            "correlationId": "2a05b9cc-32fb-41b6-a099-0fca3bb39ce2",
            "items": [
                {
                    "digest": "sha256:3c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b",
                    "urls": [
                        "http://example.com/image1.bin",
                        "http://backup.example.com/image1.bin"
                    ],
                    "sha256": "36f028580bb02cc8272a9a020f4200e346e276ae664e45ee80745574e2f5ab80",
                    "size": 1000,
                    "decryptInfo": {
                        "blockAlg": "AES256/CBC/pkcs7",
                        "blockIv": "YmxvY2tJdg==",
                        "blockKey": "YmxvY2tLZXk="
                    },
                    "signInfo": {
                        "chainName": "chainName",
                        "alg": "RSA/SHA256",
                        "value": "dmFsdWU=",
                        "trustedTimestamp": "2023-10-01T12:00:00Z",
                        "ocspValues": [
                            "ocspValue1",
                            "ocspValue2"
                        ]
                    }
                }
            ]
        }
    })";
    const auto     cExpectedAckMsg
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"f2df3016-de31-46e6-9ce8-9cde4ed7849f"\},)"
                     R"("data":\{"messageType":"ack"\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID(cRequestCorrelationID);
    mUUIDProvider.SetUUID(cRequestTxnID);

    StaticArray<StaticString<oci::cDigestLen>, 1> digests;
    digests.EmplaceBack(cDigest);

    auto blobsInfo = std::make_unique<StaticArray<BlobInfo, 1>>();

    auto future = std::async(std::launch::async,
        [this, &digests, &blobsInfo]() { return mCommunication.GetBlobsInfos(digests, *blobsInfo); });

    const auto cUrlsRequest = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cUrlsRequest, cExpectedMessage)) << "Received message: " << cUrlsRequest;

    mCloudSendMessageQueue.Push(cCloudAck);
    mCloudSendMessageQueue.Push(cResponse);

    auto err = future.get();
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(blobsInfo->Size(), 1);
    const auto& blobInfo = (*blobsInfo)[0];

    EXPECT_STREQ(blobInfo.mDigest.CStr(), cDigest);
    EXPECT_EQ(blobInfo.mURLs.Size(), 2);

    const auto cAck = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cAck, cExpectedAckMsg)) << "Received message: " << cAck;

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendMonitoring)
{
    const auto cExpectedMessage
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"monitoringData","nodes":\[\]\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("fb6e8461-2601-4f9a-8957-7ab4e52f304c");

    auto monitoring = std::make_unique<Monitoring>();

    auto err = mCommunication.SendMonitoring(*monitoring);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(std::regex_match(mCloudReceivedMessages.Pop().value_or(""), cExpectedMessage));

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendLog)
{
    const auto cExpectedMessage
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"pushLog","node":\{"codename":""\},)"
                     R"("part":0,"partsCount":0,"content":"","status":"ok"\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("fb6e8461-2601-4f9a-8957-7ab4e52f304c");

    auto log = std::make_unique<PushLog>();

    auto err = mCommunication.SendLog(*log);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    const auto cReceivedMessage = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cReceivedMessage, cExpectedMessage)) << "Received message: " << cReceivedMessage;

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendStateRequest)
{
    const auto cExpectedMessage
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"stateRequest","item":\{"id":""\},)"
                     R"("subject":\{"id":""\},"instance":0,"default":false\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("fb6e8461-2601-4f9a-8957-7ab4e52f304c");

    auto request = std::make_unique<StateRequest>();

    auto err = mCommunication.SendStateRequest(*request);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    const auto cReceivedMessage = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cReceivedMessage, cExpectedMessage)) << "Received message: " << cReceivedMessage;

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendNewState)
{
    const auto cExpectedMessage
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"newState","item":\{"id":""\},)"
                     R"("subject":\{"id":""\},"instance":0,"stateChecksum":"","state":""\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("fb6e8461-2601-4f9a-8957-7ab4e52f304c");

    auto state = std::make_unique<NewState>();

    auto err = mCommunication.SendNewState(*state);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    const auto cReceivedMessage = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cReceivedMessage, cExpectedMessage)) << "Received message: " << cReceivedMessage;

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveUpdateStateMessage)
{
    constexpr auto cMessage = R"({
        "header": {
            "systemID": "test_system_id",
            "version": 7,
            "txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"
        },
        "data": {
            "messageType": "updateState",
            "item": {
                "id": "itemID"
            },
            "subject": {
                "id": "subjectID"
            },
            "instance": 0,
            "stateChecksum": "746573745f636865636b73756d",
            "state": "test_state"
        }
    })";

    SubscribeAndWaitConnected();

    std::promise<void> messageHandled;

    EXPECT_CALL(mStateHandlerMock, UpdateState).WillOnce(Invoke([&messageHandled](const UpdateState& state) {
        EXPECT_STREQ(state.mItemID.CStr(), "itemID");
        EXPECT_STREQ(state.mSubjectID.CStr(), "subjectID");
        EXPECT_EQ(state.mInstance, 0);
        EXPECT_STREQ(state.mState.CStr(), "test_state");

        messageHandled.set_value();

        return ErrorEnum::eNone;
    }));

    mCloudSendMessageQueue.Push(cMessage);

    EXPECT_EQ(messageHandled.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveStateAcceptanceMessage)
{
    constexpr auto cMessage = R"({
        "header": {
            "systemID": "test_system_id",
            "version": 7,
            "txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"
        },
        "data": {
            "messageType": "stateAcceptance",
            "item": {
                "id": "itemID"
            },
            "subject": {
                "id": "subjectID"
            },
            "instance": 0,
            "checksum": "746573745f636865636b73756d",
            "result": "accepted",
            "reason": "test"
        }
    })";

    SubscribeAndWaitConnected();

    std::promise<void> messageHandled;

    EXPECT_CALL(mStateHandlerMock, AcceptState).WillOnce(Invoke([&messageHandled](const StateAcceptance& state) {
        EXPECT_STREQ(state.mItemID.CStr(), "itemID");
        EXPECT_STREQ(state.mSubjectID.CStr(), "subjectID");
        EXPECT_EQ(state.mInstance, 0);
        EXPECT_EQ(state.mResult.GetValue(), StateResultEnum::eAccepted);
        EXPECT_STREQ(state.mReason.CStr(), "test");

        messageHandled.set_value();

        return ErrorEnum::eNone;
    }));

    mCloudSendMessageQueue.Push(cMessage);

    EXPECT_EQ(messageHandled.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);

    auto err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveRenewCertsNotification)
{
    constexpr auto cMessage = R"({
        "header": {
            "systemID": "test_system_id",
            "version": 7,
            "txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"
        },
        "data": {
            "messageType": "renewCertificatesNotification",
            "certificates": [
                {
                    "type": "iam",
                    "node": {
                        "codename": "node0"
                    },
                    "serial": "serial_1"
                },
                {
                    "type": "iam",
                    "node": {
                        "codename": "node1"
                    },
                    "serial": "serial_2"
                }
            ],
            "unitSecrets": {
                "version": "v1.0.0",
                "nodes": [
                    {
                        "node": {
                            "codename": "node0"
                        },
                        "secret": "secret0"
                    },
                    {
                        "node": {
                            "codename": "node1"
                        },
                        "secret": "secret1"
                    }
                ]
            }
        }
    })";
    const auto     cExpectedAckMsg
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"ack"\}\}$)");
    const auto cExpectedIssuedCertsRequestMsg
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"180d54e5-0bac-4d4e-a144-68de544cb3d8"\},)"
                     R"("data":\{"messageType":"issueUnitCertificates","requests":\[)"
                     R"(\{"type":"iam","node":\{"codename":"node0"\},"csr":"csr_result_0"\},)"
                     R"(\{"type":"iam","node":\{"codename":"node1"\},"csr":"csr_result_1"\}]\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("180d54e5-0bac-4d4e-a144-68de544cb3d8");

    size_t                          createKeyRequest = 0;
    std::vector<std::promise<void>> createKeyPromises(2);

    EXPECT_CALL(mCertHandlerMock, CreateKey)
        .Times(2)
        .WillRepeatedly(Invoke([&createKeyPromises, &createKeyRequest](const String& nodeID, const String& certType,
                                   const String& subject, const String& password, String& csr) {
            EXPECT_STREQ(nodeID.CStr(), std::string("node").append(std::to_string(createKeyRequest)).c_str());
            EXPECT_STREQ(certType.CStr(), "iam");
            EXPECT_TRUE(subject.IsEmpty());
            EXPECT_STREQ(password.CStr(), std::string("secret").append(std::to_string(createKeyRequest)).c_str());

            csr.Append("csr_result_").Append(std::to_string(createKeyRequest).c_str());

            createKeyPromises[createKeyRequest].set_value();
            ++createKeyRequest;

            return ErrorEnum::eNone;
        }));

    mCloudSendMessageQueue.Push(cMessage);

    for (auto& promise : createKeyPromises) {
        EXPECT_EQ(promise.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);
    }

    const auto cReceivedAckMsg = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cReceivedAckMsg, cExpectedAckMsg)) << "Received message: " << cReceivedAckMsg;

    const auto cReceivedIssuedCertsRequestMsg = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cReceivedIssuedCertsRequestMsg, cExpectedIssuedCertsRequestMsg))
        << "Received message: " << cReceivedIssuedCertsRequestMsg;

    auto err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveIssuedUnitCerts)
{
    constexpr auto cExpectedCertsCount = 6;
    constexpr auto cMessage            = R"({
        "header": {
            "systemID": "test_system_id",
            "version": 7,
            "txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"
        },
        "data": {
            "messageType": "issuedUnitCertificates",
            "certificates": [
                {
                    "type": "iam",
                    "node": {
                        "codename": "node2"
                    },
                    "certificateChain": "chain2"
                },
                {
                    "type": "cm",
                    "node": {
                        "codename": "node2"
                    },
                    "certificateChain": "chain2"
                },
                {
                    "type": "iam",
                    "node": {
                        "codename": "node0"
                    },
                    "certificateChain": "chain0"
                },
                {
                    "type": "cm",
                    "node": {
                        "codename": "node0"
                    },
                    "certificateChain": "chain0"
                },
                {
                    "type": "iam",
                    "node": {
                        "codename": "node1"
                    },
                    "certificateChain": "chain1"
                },
                {
                    "type": "cm",
                    "node": {
                        "codename": "node1"
                    },
                    "certificateChain": "chain1"
                }
            ]
        }
    })";
    const auto     cExpectedAckMsg
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"ack"\}\}$)");
    const auto cExpectedIssuedCertsRequestMsg
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"180d54e5-0bac-4d4e-a144-68de544cb3d8"\},)"
                     R"("data":\{"messageType":"installUnitCertificatesConfirmation","certificates":\[)"
                     R"(\{"type":"cm","node":\{"codename":"node1"\},"serial":"00"\},)"
                     R"(\{"type":"iam","node":\{"codename":"node1"\},"serial":"01"\},)"
                     R"(\{"type":"cm","node":\{"codename":"node2"\},"serial":"02"\},)"
                     R"(\{"type":"iam","node":\{"codename":"node2"\},"serial":"03"\},)"
                     R"(\{"type":"cm","node":\{"codename":"node0"\},"serial":"04"\},)"
                     R"(\{"type":"iam","node":\{"codename":"node0"\},"serial":"05"\}]\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("180d54e5-0bac-4d4e-a144-68de544cb3d8");

    size_t                          applyCertRequest = 0;
    std::vector<std::promise<void>> applyCertPromises(cExpectedCertsCount);

    EXPECT_CALL(mCertHandlerMock, ApplyCert)
        .Times(cExpectedCertsCount)
        .WillRepeatedly(Invoke(
            [&applyCertPromises, &applyCertRequest](const String&, const String&, const String&, CertInfo& certInfo) {
                applyCertPromises[applyCertRequest].set_value();

                certInfo.mSerial.PushBack(static_cast<uint8_t>(applyCertRequest));

                ++applyCertRequest;

                return ErrorEnum::eNone;
            }));

    mCloudSendMessageQueue.Push(cMessage);

    for (auto& promise : applyCertPromises) {
        EXPECT_EQ(promise.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);
    }

    const auto cReceivedAckMsg = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cReceivedAckMsg, cExpectedAckMsg)) << "Received message: " << cReceivedAckMsg;

    const auto cReceivedIssuedCertsRequestMsg = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cReceivedIssuedCertsRequestMsg, cExpectedIssuedCertsRequestMsg))
        << "Received message: " << cReceivedIssuedCertsRequestMsg;

    auto err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceivedAckIsNotRecent)
{
    mConfig.mCloudResponseWaitTimeout = Time::cMilliseconds;

    constexpr auto cJSON = R"({
        "header": {
            "version": 7,
            "systemId": "test_system_id",
            "txn": "fb6e8461-2601-4f9a-8957-7ab4e52f304c"
        },
        "data": {
            "messageType": "requestLog",
            "correlationId": "logID",
            "logType": "systemLog",
            "filter": {
                "from": "2024-01-01T12:00:00Z",
                "till": "2024-01-31T12:00:00Z",
                "nodeIds": [
                    {
                        "codename": "node1"
                    },
                    {
                        "codename": "node2"
                    }
                ],
                "item": {
                    "id": "itemID"
                },
                "subject": {
                    "id": "subjectID"
                },
                "instance": 1
            }
        }
    })";
    const auto     cExpectedAckMsg
        = std::regex(R"(^\{"header":\{"version":7,"systemId":"test_system_id","createdAt":"[^"]+",)"
                     R"("txn":"fb6e8461-2601-4f9a-8957-7ab4e52f304c"\},)"
                     R"("data":\{"messageType":"ack"\}\}$)");

    SubscribeAndWaitConnected();

    mUUIDProvider.SetUUID("00008461-2601-4f9a-8957-7ab4e52f304c");

    mCloudSendMessageQueue.Push(cJSON);

    const auto cReceivedMessage = mCloudReceivedMessages.Pop().value_or("");
    EXPECT_TRUE(std::regex_match(cReceivedMessage, cExpectedAckMsg)) << "Received message: " << cReceivedMessage;

    const auto recievedCloudMessage = mCloudReceivedMessages.Pop(std::chrono::seconds(1));

    EXPECT_FALSE(recievedCloudMessage.has_value())
        << "No more messages expected, but received: " << recievedCloudMessage.value();

    auto err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace aos::cm::communication
