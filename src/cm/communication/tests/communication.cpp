/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <future>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/crypto/certloader.hpp>
#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tests/crypto/softhsmenv.hpp>
#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/common/tools/fs.hpp>
#include <core/common/types/state.hpp>
#include <core/iam/certhandler/certmodules/pkcs11/pkcs11.hpp>
#include <core/iam/tests/mocks/nodeinfoprovidermock.hpp>

#include <common/tests/stubs/storagestub.hpp>
#include <common/utils/cryptohelper.hpp>
#include <common/utils/pkcs11helper.hpp>

#include <cm/communication/cloudprotocol/servicediscovery.hpp>
#include <cm/communication/communication.hpp>

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

/***********************************************************************************************************************
 * Mocks
 **********************************************************************************************************************/

class IdentityProviderMock : public iamclient::IdentProviderItf {
public:
    MOCK_METHOD(Error, GetSystemInfo, (SystemInfo & info), (override));
    MOCK_METHOD(Error, GetSubjects, (Array<StaticString<cIDLen>> & subjects), (override));
    MOCK_METHOD(Error, SubscribeListener, (iamclient::SubjectsListenerItf & subjectsListener), (override));
    MOCK_METHOD(Error, UnsubscribeListener, (iamclient::SubjectsListenerItf & subjectsListener), (override));
};

class UpdateManagerMock : public updatemanager::UpdateManagerItf {
public:
    MOCK_METHOD(Error, ProcessDesiredStatus, (const DesiredStatus& desiredStatus), (override));
};

class StateHandlerMock : public storagestate::StateHandlerItf {
public:
    MOCK_METHOD(Error, UpdateState, (const aos::UpdateState& state), (override));
    MOCK_METHOD(Error, AcceptState, (const StateAcceptance& state), (override));
};

class LogProviderMock : public smcontroller::LogProviderItf {
public:
    MOCK_METHOD(Error, RequestLog, (const aos::RequestLog& log), (override));
};

class EnvVarHandlerMock : public launcher::EnvVarHandlerItf {
public:
    MOCK_METHOD(Error, OverrideEnvVars, (const OverrideEnvVarsRequest& envVars), (override));
};

class CertHandlerMock : public iamclient::CertHandlerItf {
public:
    MOCK_METHOD(Error, CreateKey,
        (const String& nodeID, const String& certType, const String& subject, const String& password, String& csr),
        (override));
    MOCK_METHOD(Error, ApplyCert,
        (const String& nodeID, const String& certType, const String& pemCert, CertInfo& certInfo), (override));
};

class ProvisioningMock : public iamclient::ProvisioningItf {
public:
    MOCK_METHOD(
        Error, GetCertTypes, (const String& nodeID, Array<StaticString<cCertTypeLen>>& certTypes), (const, override));
    MOCK_METHOD(Error, StartProvisioning, (const String& nodeID, const String& password), (override));
    MOCK_METHOD(Error, FinishProvisioning, (const String& nodeID, const String& password), (override));
    MOCK_METHOD(Error, Deprovision, (const String& nodeID, const String& password), (override));
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

        mConfig.mServiceDiscoveryURL = cDiscoveryServerURL;
        mConfig.mCrypt.mCACert       = CERTIFICATES_CM_DIR "/ca.cer";
        mConfig.mCertStorage         = "client";

        EXPECT_CALL(mIdentityProvider, GetSystemInfo).WillRepeatedly(Invoke([this](SystemInfo& info) {
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

        RegisterPKCS11Module(mConfig.mCertStorage.c_str());
        ASSERT_TRUE(mCertHandler.SetOwner(mConfig.mCertStorage.c_str(), cPIN).IsNone());

        RegisterPKCS11Module("server");

        ApplyCertificate(mConfig.mCertStorage.c_str(), mConfig.mCertStorage.c_str(),
            CERTIFICATES_CM_DIR "/client_int.key", CERTIFICATES_CM_DIR "/client_int.cer", 0x3333444, mClientInfo);
        ApplyCertificate("server", "localhost", CERTIFICATES_CM_DIR "/server_int.key",
            CERTIFICATES_CM_DIR "/server_int.cer", 0x3333333, mServerInfo);

        CertInfo certInfo;
        mCertHandler.GetCert("client", {}, {}, certInfo);
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
        EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillRepeatedly(Invoke([this](NodeInfo& info) {
            info.mNodeID = mNodeID;

            return ErrorEnum::eNone;
        }));

        auto err = mCommunication.Init(mConfig, mNodeInfoProvider, mIdentityProvider, mCertProvider, mCertLoader,
            mCryptoProvider, mUpdateManager, mStateHandler, mLogProvider, mEnvVarHandler, mCertHandlerMock,
            mProvisioningMock);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        err = mCommunication.SubscribeListener(mConnectionSubscriber);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        err = mCommunication.Start();
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        err = mConnectionSubscriber.WaitEvent(cConnectedEvent);
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

    StaticString<cIDLen>                        mSystemID = "test_system_id";
    StaticString<cIDLen>                        mNodeID   = "node0";
    config::Config                              mConfig;
    ConnectionSubscriberStub                    mConnectionSubscriber;
    iam::nodeinfoprovider::NodeInfoProviderMock mNodeInfoProvider;
    IdentityProviderMock                        mIdentityProvider;
    UpdateManagerMock                           mUpdateManager;
    StateHandlerMock                            mStateHandler;
    LogProviderMock                             mLogProvider;
    EnvVarHandlerMock                           mEnvVarHandler;
    CertHandlerMock                             mCertHandlerMock;
    ProvisioningMock                            mProvisioningMock;

    std::optional<HTTPServer>     mDiscoveryServer;
    std::optional<HTTPServer>     mCloudServer;
    iam::certhandler::CertHandler mCertHandler;
    CertInfo                      mClientInfo;
    CertInfo                      mServerInfo;
    crypto::DefaultCryptoProvider mCryptoProvider;
    iamclient::CertProviderStub   mCertProvider {mCertHandler};
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

    auto err = mConnectionSubscriber.WaitEvent(cDisconnectedEvent, std::chrono::seconds(15));
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    StartHTTPServer();

    err = mConnectionSubscriber.WaitEvent(cConnectedEvent);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mConnectionSubscriber.WaitEvent(cDisconnectedEvent);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mCommunication.UnsubscribeListener(mConnectionSubscriber);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SubscribeUnsubscribe)
{
    SubscribeAndWaitConnected();

    auto err = mCommunication.SubscribeListener(mConnectionSubscriber);
    ASSERT_TRUE(err.Is(ErrorEnum::eAlreadyExist)) << tests::utils::ErrorToStr(err);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mConnectionSubscriber.WaitEvent(cDisconnectedEvent);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mCommunication.UnsubscribeListener(mConnectionSubscriber);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mCommunication.UnsubscribeListener(mConnectionSubscriber);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendAlerts)
{
    constexpr auto cExpectedMessage = R"({"header":{"version":7,"systemId":"test_system_id"},)"
                                      R"("data":{"messageType":"alerts","items":[]}})";

    SubscribeAndWaitConnected();

    // cppcheck-suppress templateRecursion
    auto alerts = std::make_unique<Alerts>();

    auto err = mCommunication.SendAlerts(*alerts);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(mCloudReceivedMessages.Pop().value_or("").c_str(), cExpectedMessage);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendOverrideEnvsStatuses)
{
    constexpr auto cExpectedMessage = R"({"header":{"version":7,"systemId":"test_system_id"},)"
                                      R"("data":{"messageType":"overrideEnvVarsStatus","statuses":[]}})";

    SubscribeAndWaitConnected();

    auto statuses = std::make_unique<OverrideEnvVarsStatuses>();

    auto err = mCommunication.SendOverrideEnvsStatuses(*statuses);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(mCloudReceivedMessages.Pop().value_or("").c_str(), cExpectedMessage);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, GetBlobsInfo)
{
    StaticArray<StaticString<oci::cDigestLen>, 2> digests;
    auto                                          blobsInfo = std::make_unique<StaticArray<BlobInfo, 2>>();

    auto err = mCommunication.GetBlobsInfo(digests, *blobsInfo);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotSupported)) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendMonitoring)
{
    constexpr auto cExpectedMessage = R"({"header":{"version":7,"systemId":"test_system_id"},)"
                                      R"("data":{"messageType":"monitoringData","nodes":[]}})";

    SubscribeAndWaitConnected();

    auto monitoring = std::make_unique<Monitoring>();

    auto err = mCommunication.SendMonitoring(*monitoring);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(mCloudReceivedMessages.Pop().value_or("").c_str(), cExpectedMessage);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendLog)
{
    constexpr auto cExpectedMessage = R"({"header":{"version":7,"systemId":"test_system_id"},)"
                                      R"("data":{"messageType":"pushLog","logId":"","node":{"id":""},)"
                                      R"("part":0,"partsCount":0,"content":"","status":"ok"}})";

    SubscribeAndWaitConnected();

    auto log = std::make_unique<PushLog>();

    auto err = mCommunication.SendLog(*log);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(mCloudReceivedMessages.Pop().value_or("").c_str(), cExpectedMessage);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendStateRequest)
{
    constexpr auto cExpectedMessage = R"({"header":{"version":7,"systemId":"test_system_id"},)"
                                      R"("data":{"messageType":"stateRequest","item":{"id":""},)"
                                      R"("subject":{"id":""},"instance":0,"default":false}})";

    SubscribeAndWaitConnected();

    auto request = std::make_unique<StateRequest>();

    auto err = mCommunication.SendStateRequest(*request);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(mCloudReceivedMessages.Pop().value_or("").c_str(), cExpectedMessage);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, SendNewState)
{
    constexpr auto cExpectedMessage = R"({"header":{"version":7,"systemId":"test_system_id"},)"
                                      R"("data":{"messageType":"newState","item":{"id":""},)"
                                      R"("subject":{"id":""},"instance":0,"stateChecksum":"","state":""}})";

    SubscribeAndWaitConnected();

    auto state = std::make_unique<NewState>();

    auto err = mCommunication.SendNewState(*state);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(mCloudReceivedMessages.Pop().value_or("").c_str(), cExpectedMessage);

    err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveUpdateStateMessage)
{
    constexpr auto cMessage = R"({
        "header": {
            "systemID": "test_system_id",
            "version": 7
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

    EXPECT_CALL(mStateHandler, UpdateState).WillOnce(Invoke([&messageHandled](const UpdateState& state) {
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
            "version": 7
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

    EXPECT_CALL(mStateHandler, AcceptState).WillOnce(Invoke([&messageHandled](const StateAcceptance& state) {
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
            "version": 7
        },
        "data": {
            "messageType": "renewCertificatesNotification",
            "certificates": [
                {
                    "type": "iam",
                    "node": {
                        "id": "node0"
                    },
                    "serial": "serial_1"
                },
                {
                    "type": "iam",
                    "node": {
                        "id": "node1"
                    },
                    "serial": "serial_2"
                }
            ],
            "unitSecrets": {
                "version": "v1.0.0",
                "nodes": [
                    {
                        "node": {
                            "id": "node0"
                        },
                        "secret": "secret0"
                    },
                    {
                        "node": {
                            "id": "node1"
                        },
                        "secret": "secret1"
                    }
                ]
            }
        }
    })";

    constexpr auto cExpectedSentMsg = R"({"header":{"version":7,"systemId":"test_system_id"},"data":)"
                                      R"({"messageType":"issueUnitCertificates","requests":[)"
                                      R"({"type":"iam","node":{"id":"node0"},"csr":"csr_result_0"},)"
                                      R"({"type":"iam","node":{"id":"node1"},"csr":"csr_result_1"}]}})";

    SubscribeAndWaitConnected();

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

    const auto sentMessage = mCloudReceivedMessages.Pop();
    EXPECT_STREQ(sentMessage.value_or("").c_str(), cExpectedSentMsg);

    auto err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMCommunicationTest, ReceiveIssuedUnitCerts)
{
    constexpr auto cExpectedCertsCount = 6;
    constexpr auto cMessage            = R"({
        "header": {
            "systemID": "test_system_id",
            "version": 7
        },
        "data": {
            "messageType": "issuedUnitCertificates",
            "certificates": [
                {
                    "type": "iam",
                    "node": {
                        "id": "node2"
                    },
                    "certificateChain": "chain2"
                },
                {
                    "type": "cm",
                    "node": {
                        "id": "node2"
                    },
                    "certificateChain": "chain2"
                },
                {
                    "type": "iam",
                    "node": {
                        "id": "node0"
                    },
                    "certificateChain": "chain0"
                },
                {
                    "type": "cm",
                    "node": {
                        "id": "node0"
                    },
                    "certificateChain": "chain0"
                },
                {
                    "type": "iam",
                    "node": {
                        "id": "node1"
                    },
                    "certificateChain": "chain1"
                },
                {
                    "type": "cm",
                    "node": {
                        "id": "node1"
                    },
                    "certificateChain": "chain1"
                }
            ]
        }
    })";

    constexpr auto cExpectedSentMsg = R"({"header":{"version":7,"systemId":"test_system_id"},"data":)"
                                      R"({"messageType":"installUnitCertificatesConfirmation","certificates":[)"
                                      R"({"type":"cm","node":{"id":"node1"},"serial":"00"},)"
                                      R"({"type":"iam","node":{"id":"node1"},"serial":"01"},)"
                                      R"({"type":"cm","node":{"id":"node2"},"serial":"02"},)"
                                      R"({"type":"iam","node":{"id":"node2"},"serial":"03"},)"
                                      R"({"type":"cm","node":{"id":"node0"},"serial":"04"},)"
                                      R"({"type":"iam","node":{"id":"node0"},"serial":"05"}]}})";

    SubscribeAndWaitConnected();

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

    const auto sentMessage = mCloudReceivedMessages.Pop();
    EXPECT_STREQ(sentMessage.value_or("").c_str(), cExpectedSentMsg);

    auto err = mCommunication.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace aos::cm::communication
