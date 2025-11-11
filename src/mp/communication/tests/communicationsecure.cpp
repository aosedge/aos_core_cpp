/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <optional>

#include <gtest/gtest.h>

#include <openssl/err.h>
#include <openssl/trace.h>

#include <core/common/crypto/certloader.hpp>
#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tests/crypto/softhsmenv.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tools/fs.hpp>
#include <core/iam/certhandler/certhandler.hpp>
#include <core/iam/certhandler/certmodules/pkcs11/pkcs11.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>
#include <servicemanager/v4/servicemanager.grpc.pb.h>

#include <common/downloader/downloader.hpp>
#include <common/utils/cryptohelper.hpp>
#include <common/utils/pkcs11helper.hpp>

#include <mp/communication/communicationmanager.hpp>
#include <mp/communication/socket.hpp>

#include "stubs/storagestub.hpp"
#include "stubs/transport.hpp"
#include "utils/generateimage.hpp"

using namespace testing;

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CertProvider : public common::iamclient::TLSCredentialsItf {
public:
    CertProvider(iam::certhandler::CertHandler& certHandler)
        : mCertHandler(certHandler)
    {
    }

    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSClientCredentials(
        [[maybe_unused]] const String& certStorage) override
    {
        return {nullptr, ErrorEnum::eNone};
    }

    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetTLSClientCredentials() override
    {
        return {nullptr, ErrorEnum::eNone};
    }

    Error GetCert(const String& certType, [[maybe_unused]] const Array<uint8_t>& issuer,
        [[maybe_unused]] const Array<uint8_t>& serial, CertInfo& resCert) const
    {
        mCertCalled = true;
        mCondVar.notify_all();

        mCertHandler.GetCert(certType, {}, {}, resCert);

        return ErrorEnum::eNone;
    }

    Error SubscribeListener(
        [[maybe_unused]] const String& certType, [[maybe_unused]] iamclient::CertListenerItf& listener) override
    {
        return ErrorEnum::eNone;
    }

    Error UnsubscribeListener([[maybe_unused]] iamclient::CertListenerItf& listener) override
    {
        return ErrorEnum::eNone;
    }

    bool IsCertCalled()
    {
        std::unique_lock lock {mMutex};

        mCondVar.wait_for(lock, cWaitTimeout, [this] { return mCertCalled.load(); });

        return mCertCalled;
    }

    void ResetCertCalled() { mCertCalled = false; }

private:
    iam::certhandler::CertHandler&  mCertHandler;
    mutable std::atomic_bool        mCertCalled {};
    std::mutex                      mMutex;
    mutable std::condition_variable mCondVar;
    constexpr static auto           cWaitTimeout = std::chrono::seconds(3);
};

class CommunicationSecureManagerTest : public ::testing::Test {
protected:
    static constexpr auto cMaxModulesCount = 3;
    static constexpr auto cPIN             = "admin";

    void SetUp() override
    {
        tests::utils::InitLog();

        std::filesystem::create_directories(mTmpDir);

        mConfig.mIAMConfig.mOpenPort             = 8081;
        mConfig.mIAMConfig.mSecurePort           = 8080;
        mConfig.mVChan.mIAMCertStorage           = "server";
        mConfig.mVChan.mSMCertStorage            = "server";
        mConfig.mDownload.mDownloadDir           = "download";
        mConfig.mImageStoreDir                   = "images";
        mConfig.mCMConfig.mOpenPort              = 30001;
        mConfig.mCMConfig.mSecurePort            = 30002;
        mConfig.mLogProviderConfig.mMaxPartSize  = 1024;
        mConfig.mLogProviderConfig.mMaxPartCount = 10;

        mConfig.mCACert = CERTIFICATES_MP_DIR "/ca.cer";

        ASSERT_TRUE(mCryptoProvider.Init().IsNone());
        ASSERT_TRUE(mSOFTHSMEnv
                        .Init("", "certhandler-integration-tests", SOFTHSM_BASE_MP_DIR "/softhsm2.conf",
                            SOFTHSM_BASE_MP_DIR "/tokens", SOFTHSM2_LIB)
                        .IsNone());
        ASSERT_TRUE(mCertLoader.Init(mCryptoProvider, mSOFTHSMEnv.GetManager()).IsNone());

        RegisterPKCS11Module("client");
        ASSERT_TRUE(mCertHandler.SetOwner("client", cPIN).IsNone());

        RegisterPKCS11Module("server");

        ApplyCertificate("client", "client", CERTIFICATES_MP_DIR "/client_int.key",
            CERTIFICATES_MP_DIR "/client_int.cer", 0x3333444, mClientInfo);
        ApplyCertificate("server", "localhost", CERTIFICATES_MP_DIR "/server_int.key",
            CERTIFICATES_MP_DIR "/server_int.cer", 0x3333333, mServerInfo);

        mServer.emplace();
        EXPECT_EQ(mServer->Init(30001), ErrorEnum::eNone);

        mClient.emplace("localhost", 30001);

        CertInfo certInfo;
        mCertHandler.GetCert("client", {}, {}, certInfo);
        auto [keyURI, errPkcs] = common::utils::CreatePKCS11URL(certInfo.mKeyURL);
        EXPECT_EQ(errPkcs, ErrorEnum::eNone);

        mKeyURI = keyURI;

        auto [certPEM, err2] = common::utils::LoadPEMCertificates(certInfo.mCertURL, mCertLoader, mCryptoProvider);
        EXPECT_EQ(err2, ErrorEnum::eNone);

        mCertPEM = certPEM;

        mCommManagerClient.emplace(mClient.value());

        mCertProvider.emplace(mCertHandler);
        mCommManager.emplace();
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

    iam::certhandler::PKCS11ModuleConfig GetPKCS11ModuleConfig()
    {
        iam::certhandler::PKCS11ModuleConfig config;

        config.mLibrary         = SOFTHSM2_LIB;
        config.mSlotID          = mSOFTHSMEnv.GetSlotID();
        config.mUserPINPath     = CERTIFICATES_MP_DIR "/pin.txt";
        config.mModulePathInURL = true;

        return config;
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

        ASSERT_TRUE(fs::ReadFileToString(CERTIFICATES_MP_DIR "/ca.cer", caCert).IsNone());
        clientCertChain.Append(caCert);

        auto err = mCertHandler.ApplyCertificate(certType, clientCertChain, certInfo);
        // apply client certificate
        LOG_DBG() << "err = " << err;
        ASSERT_TRUE(err.IsNone());
        EXPECT_EQ(certInfo.mSerial, serialArr);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(mTmpDir);
        std::filesystem::remove_all(mConfig.mDownload.mDownloadDir);
        std::filesystem::remove_all(mConfig.mImageStoreDir);

        std::filesystem::remove_all(SOFTHSM_BASE_MP_DIR "/tokens");
    }

    crypto::DefaultCryptoProvider  mCryptoProvider;
    crypto::CertLoader             mCertLoader;
    iam::certhandler::CertHandler  mCertHandler;
    CertInfo                       mClientInfo;
    CertInfo                       mServerInfo;
    common::downloader::Downloader mDownloader;
    std::optional<CertProvider>    mCertProvider;
    std::string                    mKeyURI;
    std::string                    mCertPEM;

    std::optional<Socket>       mServer;
    std::optional<SocketClient> mClient;

    std::optional<CommunicationManager> mCommManager;
    config::Config                      mConfig;

    std::shared_ptr<CommChannelItf> mIAMClientChannel;
    std::shared_ptr<CommChannelItf> mCMClientChannel;
    std::shared_ptr<CommChannelItf> mOpenCMClientChannel;

    std::optional<SecureClientChannel> mIAMSecurePipe;
    std::optional<SecureClientChannel> mCMSecurePipe;
    std::optional<CommManager>         mCommManagerClient;
    Handler                            IAMOpenHandler {};
    Handler                            IAMSecureHandler {};
    Handler                            CMHandler {};

    std::string mTmpDir {"tmp"};

private:
    test::SoftHSMEnv                                              mSOFTHSMEnv;
    iam::certhandler::StorageStub                                 mStorage;
    StaticArray<iam::certhandler::PKCS11Module, cMaxModulesCount> mPKCS11Modules;
    StaticArray<iam::certhandler::CertModule, cMaxModulesCount>   mCertModules;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CommunicationSecureManagerTest, TestSecureChannel)
{
    IAMConnection mIAMOpenConnection {};
    IAMConnection mIAMSecureConnection {};
    CMConnection  mCMConnection {};

    mIAMClientChannel = mCommManagerClient->CreateCommChannel(8080);
    mIAMSecurePipe.emplace(*mIAMClientChannel, mKeyURI, mCertPEM, CERTIFICATES_MP_DIR "/ca.cer");

    mCMClientChannel = mCommManagerClient->CreateCommChannel(30002);
    mCMSecurePipe.emplace(*mCMClientChannel, mKeyURI, mCertPEM, CERTIFICATES_MP_DIR "/ca.cer");

    mOpenCMClientChannel = mCommManagerClient->CreateCommChannel(30001);

    auto err = mCommManager->Init(mConfig, mServer.value(), &mCertLoader, &mCryptoProvider);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mIAMOpenConnection.Init(mConfig.mIAMConfig.mOpenPort, IAMOpenHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mIAMSecureConnection.Init(mConfig.mIAMConfig.mSecurePort, IAMSecureHandler, *mCommManager,
        &mCertProvider.value(), mConfig.mVChan.mIAMCertStorage);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager, &mDownloader, &mCertProvider.value());
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mIAMOpenConnection.Start(), ErrorEnum::eNone);
    EXPECT_EQ(mIAMSecureConnection.Start(), ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), ErrorEnum::eNone);

    // connect to IAM
    EXPECT_EQ(mIAMSecurePipe->Connect(), ErrorEnum::eNone);

    // connect to CM
    EXPECT_EQ(mCMSecurePipe->Connect(), ErrorEnum::eNone);

    // send message to IAM
    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    outgoingMsg.mutable_start_provisioning_response();
    std::vector<uint8_t> messageData(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(messageData.data(), messageData.size()));

    auto protobufHeader = PrepareProtobufHeader(messageData.size());
    protobufHeader.insert(protobufHeader.end(), messageData.begin(), messageData.end());
    EXPECT_EQ(mIAMSecurePipe->Write(protobufHeader), ErrorEnum::eNone);

    auto [receivedMsg, errReceive] = IAMSecureHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive, ErrorEnum::eNone);
    EXPECT_TRUE(outgoingMsg.ParseFromArray(receivedMsg.data(), receivedMsg.size()));
    EXPECT_TRUE(outgoingMsg.has_start_provisioning_response());

    // send message to CM
    servicemanager::v4::SMOutgoingMessages smOutgoingMessages;
    smOutgoingMessages.mutable_node_config_status();
    std::vector<uint8_t> messageData2(smOutgoingMessages.ByteSizeLong());
    EXPECT_TRUE(smOutgoingMessages.SerializeToArray(messageData2.data(), messageData2.size()));

    protobufHeader = PrepareProtobufHeader(messageData2.size());
    protobufHeader.insert(protobufHeader.end(), messageData2.begin(), messageData2.end());

    EXPECT_EQ(mCMSecurePipe->Write(protobufHeader), ErrorEnum::eNone);

    auto [receivedMsg2, errReceive2] = CMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive2, ErrorEnum::eNone);

    EXPECT_TRUE(smOutgoingMessages.ParseFromArray(receivedMsg2.data(), receivedMsg2.size()));
    EXPECT_TRUE(smOutgoingMessages.has_node_config_status());

    mCommManager->Stop();
    mCommManagerClient->Close();
    mIAMOpenConnection.Stop();
    mIAMSecureConnection.Stop();
    mCMConnection.Stop();
    mIAMSecurePipe->Close();
    mCMSecurePipe->Close();
}

TEST_F(CommunicationSecureManagerTest, TestIAMFlow)
{
    IAMConnection mIAMSecureConnection {};

    mIAMClientChannel = mCommManagerClient->CreateCommChannel(8080);
    mIAMSecurePipe.emplace(*mIAMClientChannel, mKeyURI, mCertPEM, CERTIFICATES_MP_DIR "/ca.cer");

    auto err = mCommManager->Init(mConfig, mServer.value(), &mCertLoader, &mCryptoProvider);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mIAMSecureConnection.Init(mConfig.mIAMConfig.mSecurePort, IAMSecureHandler, *mCommManager,
        &mCertProvider.value(), mConfig.mVChan.mIAMCertStorage);
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mIAMSecureConnection.Start(), ErrorEnum::eNone);

    // connect to IAM
    EXPECT_EQ(mIAMSecurePipe->Connect(), ErrorEnum::eNone);

    iamanager::v5::IAMIncomingMessages incomingMsg;
    incomingMsg.mutable_start_provisioning_request();
    std::vector<uint8_t> messageData(incomingMsg.ByteSizeLong());
    EXPECT_TRUE(incomingMsg.SerializeToArray(messageData.data(), messageData.size()));
    EXPECT_EQ(IAMSecureHandler.SetIncomingMessages(messageData), ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mIAMSecurePipe->Read(message), ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mIAMSecurePipe->Read(message), ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(message.data(), message.size()));
    EXPECT_TRUE(incomingMsg.has_start_provisioning_request());

    // send message to IAM
    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    outgoingMsg.mutable_start_provisioning_response();
    messageData.clear();
    messageData.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(messageData.data(), messageData.size()));

    auto protobufHeader = PrepareProtobufHeader(messageData.size());
    protobufHeader.insert(protobufHeader.end(), messageData.begin(), messageData.end());
    EXPECT_EQ(mIAMSecurePipe->Write(protobufHeader), ErrorEnum::eNone);

    auto [receivedMsg, errReceive] = IAMSecureHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive, ErrorEnum::eNone);
    EXPECT_TRUE(outgoingMsg.ParseFromArray(receivedMsg.data(), receivedMsg.size()));
    EXPECT_TRUE(outgoingMsg.has_start_provisioning_response());

    mCommManager->Stop();
    mCommManagerClient->Close();
    mIAMSecureConnection.Stop();
    mIAMSecurePipe->Close();
}

TEST_F(CommunicationSecureManagerTest, TestSendCMFlow)
{
    CMConnection mCMConnection {};

    mCMClientChannel = mCommManagerClient->CreateCommChannel(30002);
    mCMSecurePipe.emplace(*mCMClientChannel, mKeyURI, mCertPEM, CERTIFICATES_MP_DIR "/ca.cer");

    auto err = mCommManager->Init(mConfig, mServer.value(), &mCertLoader, &mCryptoProvider);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager, &mDownloader, &mCertProvider.value());
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), ErrorEnum::eNone);

    // connect to CM
    EXPECT_EQ(mCMSecurePipe->Connect(), ErrorEnum::eNone);

    servicemanager::v4::SMIncomingMessages incomingMsg;
    incomingMsg.mutable_get_node_config_status();
    std::vector<uint8_t> messageData(incomingMsg.ByteSizeLong());
    EXPECT_TRUE(incomingMsg.SerializeToArray(messageData.data(), messageData.size()));
    EXPECT_EQ(CMHandler.SetIncomingMessages(messageData), ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mCMSecurePipe->Read(message), ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mCMSecurePipe->Read(message), ErrorEnum::eNone);
    servicemanager::v4::SMIncomingMessages incomingMessages;
    EXPECT_TRUE(incomingMessages.ParseFromArray(message.data(), message.size()));
    EXPECT_TRUE(incomingMessages.has_get_node_config_status());

    servicemanager::v4::SMOutgoingMessages smOutgoingMessages;
    smOutgoingMessages.mutable_node_config_status();
    std::vector<uint8_t> messageData2(smOutgoingMessages.ByteSizeLong());
    EXPECT_TRUE(smOutgoingMessages.SerializeToArray(messageData2.data(), messageData2.size()));

    auto protobufHeader = PrepareProtobufHeader(messageData2.size());
    protobufHeader.insert(protobufHeader.end(), messageData2.begin(), messageData2.end());

    EXPECT_EQ(mCMSecurePipe->Write(protobufHeader), ErrorEnum::eNone);

    auto [receivedMsg2, errReceive2] = CMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive2, ErrorEnum::eNone);

    EXPECT_TRUE(smOutgoingMessages.ParseFromArray(receivedMsg2.data(), receivedMsg2.size()));
    EXPECT_TRUE(smOutgoingMessages.has_node_config_status());

    mCommManager->Stop();
    mCommManagerClient->Close();
    mCMConnection.Stop();
    mCMSecurePipe->Close();
}

TEST_F(CommunicationSecureManagerTest, TestDownload)
{
    CMConnection mCMConnection {};

    mCMClientChannel = mCommManagerClient->CreateCommChannel(30002);
    mCMSecurePipe.emplace(*mCMClientChannel, mKeyURI, mCertPEM, CERTIFICATES_MP_DIR "/ca.cer");

    auto err = mCommManager->Init(mConfig, mServer.value(), &mCertLoader, &mCryptoProvider);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager, &mDownloader, &mCertProvider.value());
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), ErrorEnum::eNone);

    // connect to CM
    EXPECT_EQ(mCMSecurePipe->Connect(), ErrorEnum::eNone);

    std::string archivePath = PrepareService(mTmpDir);

    // send message to IAM
    servicemanager::v4::SMOutgoingMessages outgoingMsg;
    outgoingMsg.mutable_image_content_request();
    outgoingMsg.mutable_image_content_request()->set_url("file://" + std::filesystem::absolute(archivePath).string());
    outgoingMsg.mutable_image_content_request()->set_request_id(1);
    outgoingMsg.mutable_image_content_request()->set_content_type("service");

    std::vector<uint8_t> messageData(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(messageData.data(), messageData.size()));

    auto protobufHeader = PrepareProtobufHeader(messageData.size());
    protobufHeader.insert(protobufHeader.end(), messageData.begin(), messageData.end());
    EXPECT_EQ(mCMSecurePipe->Write(protobufHeader), ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mCMSecurePipe->Read(message), ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mCMSecurePipe->Read(message), ErrorEnum::eNone);
    servicemanager::v4::SMIncomingMessages incomingMessages;
    EXPECT_TRUE(incomingMessages.ParseFromArray(message.data(), message.size()));
    EXPECT_TRUE(incomingMessages.has_image_content_info());

    auto imageCount = incomingMessages.image_content_info().image_files_size();

    EXPECT_EQ(imageCount, 4);

    bool foundService {};
    for (int i = 0; i < imageCount; i++) {
        std::vector<uint8_t> message(sizeof(AosProtobufHeader));
        EXPECT_EQ(mCMSecurePipe->Read(message), ErrorEnum::eNone);
        auto header = ParseProtobufHeader(message);
        message.clear();
        message.resize(header.mDataSize);

        EXPECT_EQ(mCMSecurePipe->Read(message), ErrorEnum::eNone);
        servicemanager::v4::SMIncomingMessages incomingMessages;
        EXPECT_TRUE(incomingMessages.ParseFromArray(message.data(), message.size()));
        EXPECT_TRUE(incomingMessages.has_image_content());

        EXPECT_EQ(incomingMessages.image_content().request_id(), 1);
        auto content = incomingMessages.image_content().relative_path();
        if (content.find("service.py") != std::string::npos) {
            foundService = true;
        }
    }

    EXPECT_TRUE(foundService);

    mCommManager->Stop();
    mCommManagerClient->Close();
    mCMConnection.Stop();
    mCMSecurePipe->Close();
}

TEST_F(CommunicationSecureManagerTest, TestSendLog)
{
    CMConnection mCMConnection {};

    mCMClientChannel = mCommManagerClient->CreateCommChannel(30002);
    mCMSecurePipe.emplace(*mCMClientChannel, mKeyURI, mCertPEM, CERTIFICATES_MP_DIR "/ca.cer");

    auto err = mCommManager->Init(mConfig, mServer.value(), &mCertLoader, &mCryptoProvider);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager, &mDownloader, &mCertProvider.value());
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), ErrorEnum::eNone);

    // connect to CM
    EXPECT_EQ(mCMSecurePipe->Connect(), ErrorEnum::eNone);

    struct LogData {
        std::string mLogId;
        std::string mLogMessage;
        LogStatus   mStatus;
    } testLogData[] = {
        {"id1", "test log message1\n", LogStatusEnum::eOK},
        {"id1", "test log message2\n", LogStatusEnum::eOK},
        {"id1", "", LogStatusEnum::eEmpty},
    };

    for (const auto& logData : testLogData) {
        servicemanager::v4::SMOutgoingMessages outgoingMsg;
        auto&                                  log = *outgoingMsg.mutable_log();

        log.set_log_id(logData.mLogId);
        log.set_data(logData.mLogMessage);
        log.set_status(logData.mStatus.ToString().CStr());

        std::vector<uint8_t> messageData(outgoingMsg.ByteSizeLong());
        EXPECT_TRUE(outgoingMsg.SerializeToArray(messageData.data(), messageData.size()));

        auto protobufHeader = PrepareProtobufHeader(messageData.size());
        protobufHeader.insert(protobufHeader.end(), messageData.begin(), messageData.end());
        EXPECT_EQ(mCMSecurePipe->Write(protobufHeader), ErrorEnum::eNone);
    }

    auto [receivedMsg, errReceive] = CMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive, ErrorEnum::eNone);

    servicemanager::v4::SMOutgoingMessages outgoingMsg;
    EXPECT_TRUE(outgoingMsg.ParseFromArray(receivedMsg.data(), receivedMsg.size()));

    EXPECT_EQ(outgoingMsg.SMOutgoingMessage_case(), servicemanager::v4::SMOutgoingMessages::kLog);
    EXPECT_EQ(outgoingMsg.log().log_id(), "id1");
    EXPECT_EQ(outgoingMsg.log().status(), LogStatus(LogStatusEnum::eOK).ToString().CStr());

    mCommManager->Stop();
    mCommManagerClient->Close();
    mCMConnection.Stop();
    mCMSecurePipe->Close();
}

TEST_F(CommunicationSecureManagerTest, DISABLED_TestCertChange)
{
    IAMConnection mIAMSecureConnection {};

    mIAMClientChannel = mCommManagerClient->CreateCommChannel(8080);
    mIAMSecurePipe.emplace(*mIAMClientChannel, mKeyURI, mCertPEM, CERTIFICATES_MP_DIR "/ca.cer");

    auto err = mCommManager->Init(mConfig, mServer.value(), &mCertLoader, &mCryptoProvider);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mIAMSecureConnection.Init(mConfig.mIAMConfig.mSecurePort, IAMSecureHandler, *mCommManager,
        &mCertProvider.value(), mConfig.mVChan.mIAMCertStorage);
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mIAMSecureConnection.Start(), ErrorEnum::eNone);

    EXPECT_EQ(mIAMSecurePipe->Connect(), ErrorEnum::eNone);

    EXPECT_TRUE(mCertProvider->IsCertCalled());

    mCommManager->OnCertChanged(CertInfo {});
    mIAMSecurePipe->Close();
    mCertProvider->ResetCertCalled();

    EXPECT_TRUE(mClient->WaitForConnection());

    EXPECT_EQ(mIAMSecurePipe->Connect(), ErrorEnum::eNone);

    EXPECT_TRUE(mCertProvider->IsCertCalled());

    mCommManager->Stop();
    mCommManagerClient->Close();
    mIAMSecureConnection.Stop();
    mIAMSecurePipe->Close();
}

} // namespace aos::mp::communication
