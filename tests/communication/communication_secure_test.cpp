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

#include <aos/common/crypto/mbedtls/cryptoprovider.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/certmodules/pkcs11/pkcs11.hpp>
#include <downloader/downloader.hpp>
#include <utils/cryptohelper.hpp>
#include <utils/pkcs11helper.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>
#include <servicemanager/v4/servicemanager.grpc.pb.h>

#include <aos/test/log.hpp>
#include <aos/test/softhsmenv.hpp>

#include "communication/communicationmanager.hpp"
#include "communication/socket.hpp"

#include "stubs/storagestub.hpp"
#include "stubs/transport.hpp"
#include "utils/generateimage.hpp"

using namespace testing;
using namespace aos::common::iamclient;
using namespace aos::mp::communication;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CertProvider : public TLSCredentialsItf {
public:
    CertProvider(aos::iam::certhandler::CertHandler& certHandler)
        : mCertHandler(certHandler)
    {
    }

    aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSClientCredentials(
        [[maybe_unused]] const aos::String& certStorage) override
    {
        return {nullptr, aos::ErrorEnum::eNone};
    }

    aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetTLSClientCredentials() override
    {
        return {nullptr, aos::ErrorEnum::eNone};
    }

    // aos::Error GetCertificate(const std::string& certType, aos::iam::certhandler::CertInfo& certInfo) override
    aos::Error GetCert(const aos::String& certType, [[maybe_unused]] const aos::Array<uint8_t>& issuer,
        [[maybe_unused]] const aos::Array<uint8_t>& serial, aos::iam::certhandler::CertInfo& resCert) const
    {
        mCertCalled = true;
        mCondVar.notify_all();

        mCertHandler.GetCertificate(certType, {}, {}, resCert);

        return aos::ErrorEnum::eNone;
    }

    aos::Error SubscribeCertChanged([[maybe_unused]] const aos::String& certType,
        [[maybe_unused]] aos::iam::certhandler::CertReceiverItf&        subscriber) override
    {
        return aos::ErrorEnum::eNone;
    }

    aos::Error UnsubscribeCertChanged([[maybe_unused]] aos::iam::certhandler::CertReceiverItf& subscriber) override
    {
        return aos::ErrorEnum::eNone;
    }

    bool IsCertCalled()
    {
        std::unique_lock lock {mMutex};

        mCondVar.wait_for(lock, cWaitTimeout, [this] { return mCertCalled.load(); });

        return mCertCalled;
    }

    void ResetCertCalled() { mCertCalled = false; }

private:
    aos::iam::certhandler::CertHandler& mCertHandler;
    mutable std::atomic_bool            mCertCalled {};
    std::mutex                          mMutex;
    mutable std::condition_variable     mCondVar;
    constexpr static auto               cWaitTimeout = std::chrono::seconds(3);
};

class CommunicationSecureManagerTest : public ::testing::Test {
protected:
    static constexpr auto cMaxModulesCount = 3;
    static constexpr auto cPIN             = "admin";

    void SetUp() override
    {
        aos::test::InitLog();

        std::filesystem::create_directories(mTmpDir);

        // BIO* err = BIO_new_fp(stderr, BIO_NOCLOSE | BIO_FP_TEXT);
        // OSSL_trace_set_channel(OSSL_TRACE_CATEGORY_TLS, err);
        // OSSL_trace_set_prefix(OSSL_TRACE_CATEGORY_TLS, "BEGIN TRACE[TLS]");
        // OSSL_trace_set_suffix(OSSL_TRACE_CATEGORY_TLS, "END TRACE[TLS]");

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
        EXPECT_EQ(mServer->Init(30001), aos::ErrorEnum::eNone);

        mClient.emplace("localhost", 30001);

        aos::iam::certhandler::CertInfo certInfo;
        mCertHandler.GetCertificate("client", {}, {}, certInfo);
        auto [keyURI, errPkcs] = aos::common::utils::CreatePKCS11URL(certInfo.mKeyURL);
        EXPECT_EQ(errPkcs, aos::ErrorEnum::eNone);

        mKeyURI = keyURI;

        auto [certPEM, err2] = aos::common::utils::LoadPEMCertificates(certInfo.mCertURL, mCertLoader, mCryptoProvider);
        EXPECT_EQ(err2, aos::ErrorEnum::eNone);

        mCertPEM = certPEM;

        mCommManagerClient.emplace(mClient.value());

        mCertProvider.emplace(mCertHandler);
        mCommManager.emplace();
    }

    void RegisterPKCS11Module(const aos::String& name, aos::crypto::KeyType keyType = aos::crypto::KeyTypeEnum::eRSA)
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

    aos::iam::certhandler::ModuleConfig GetCertModuleConfig(aos::crypto::KeyType keyType)
    {
        aos::iam::certhandler::ModuleConfig config;

        config.mKeyType         = keyType;
        config.mMaxCertificates = 2;
        config.mExtendedKeyUsage.EmplaceBack(aos::iam::certhandler::ExtendedKeyUsageEnum::eClientAuth);
        config.mAlternativeNames.EmplaceBack("epam.com");
        config.mAlternativeNames.EmplaceBack("www.epam.com");
        config.mSkipValidation = false;

        return config;
    }

    aos::iam::certhandler::PKCS11ModuleConfig GetPKCS11ModuleConfig()
    {
        aos::iam::certhandler::PKCS11ModuleConfig config;

        config.mLibrary         = SOFTHSM2_LIB;
        config.mSlotID          = mSOFTHSMEnv.GetSlotID();
        config.mUserPINPath     = CERTIFICATES_MP_DIR "/pin.txt";
        config.mModulePathInURL = true;

        return config;
    }

    void ApplyCertificate(const aos::String& certType, const aos::String& subject, const aos::String& intermKeyPath,
        const aos::String& intermCertPath, uint64_t serial, aos::iam::certhandler::CertInfo& certInfo)
    {
        aos::StaticString<aos::crypto::cCSRPEMLen> csr;
        ASSERT_TRUE(mCertHandler.CreateKey(certType, subject, cPIN, csr).IsNone());

        // create certificate from CSR, CA priv key, CA cert
        aos::StaticString<aos::crypto::cPrivKeyPEMLen> intermKey;
        ASSERT_TRUE(aos::fs::ReadFileToString(intermKeyPath, intermKey).IsNone());

        aos::StaticString<aos::crypto::cCertPEMLen> intermCert;
        ASSERT_TRUE(aos::fs::ReadFileToString(intermCertPath, intermCert).IsNone());

        auto serialArr = aos::Array<uint8_t>(reinterpret_cast<uint8_t*>(&serial), sizeof(serial));
        aos::StaticString<aos::crypto::cCertPEMLen> clientCertChain;

        ASSERT_TRUE(mCryptoProvider.CreateClientCert(csr, intermKey, intermCert, serialArr, clientCertChain).IsNone());

        // add intermediate cert to the chain
        clientCertChain.Append(intermCert);

        // add CA certificate to the chain
        aos::StaticString<aos::crypto::cCertPEMLen> caCert;

        ASSERT_TRUE(aos::fs::ReadFileToString(CERTIFICATES_MP_DIR "/ca.cer", caCert).IsNone());
        clientCertChain.Append(caCert);

        // apply client certificate
        ASSERT_TRUE(mCertHandler.ApplyCertificate(certType, clientCertChain, certInfo).IsNone());
        EXPECT_EQ(certInfo.mSerial, serialArr);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(mTmpDir);
        std::filesystem::remove_all(mConfig.mDownload.mDownloadDir);
        std::filesystem::remove_all(mConfig.mImageStoreDir);

        std::filesystem::remove_all(SOFTHSM_BASE_MP_DIR "/tokens");
    }

    aos::crypto::MbedTLSCryptoProvider  mCryptoProvider;
    aos::crypto::CertLoader             mCertLoader;
    aos::iam::certhandler::CertHandler  mCertHandler;
    aos::iam::certhandler::CertInfo     mClientInfo;
    aos::iam::certhandler::CertInfo     mServerInfo;
    aos::common::downloader::Downloader mDownloader;
    std::optional<CertProvider>         mCertProvider;
    std::string                         mKeyURI;
    std::string                         mCertPEM;

    std::optional<aos::mp::communication::Socket> mServer;
    std::optional<SocketClient>                   mClient;

    std::optional<CommunicationManager> mCommManager;
    aos::mp::config::Config             mConfig;

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
    aos::test::SoftHSMEnv                                                   mSOFTHSMEnv;
    aos::iam::certhandler::StorageStub                                      mStorage;
    aos::StaticArray<aos::iam::certhandler::PKCS11Module, cMaxModulesCount> mPKCS11Modules;
    aos::StaticArray<aos::iam::certhandler::CertModule, cMaxModulesCount>   mCertModules;
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
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mIAMOpenConnection.Init(mConfig.mIAMConfig.mOpenPort, IAMOpenHandler, *mCommManager);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mIAMSecureConnection.Init(mConfig.mIAMConfig.mSecurePort, IAMSecureHandler, *mCommManager,
        &mCertProvider.value(), mConfig.mVChan.mIAMCertStorage);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager, &mDownloader, &mCertProvider.value());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMOpenConnection.Start(), aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMSecureConnection.Start(), aos::ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), aos::ErrorEnum::eNone);

    // connect to IAM
    EXPECT_EQ(mIAMSecurePipe->Connect(), aos::ErrorEnum::eNone);

    // connect to CM
    EXPECT_EQ(mCMSecurePipe->Connect(), aos::ErrorEnum::eNone);

    // send message to IAM
    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    outgoingMsg.mutable_start_provisioning_response();
    std::vector<uint8_t> messageData(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(messageData.data(), messageData.size()));

    auto protobufHeader = PrepareProtobufHeader(messageData.size());
    protobufHeader.insert(protobufHeader.end(), messageData.begin(), messageData.end());
    EXPECT_EQ(mIAMSecurePipe->Write(protobufHeader), aos::ErrorEnum::eNone);

    auto [receivedMsg, errReceive] = IAMSecureHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive, aos::ErrorEnum::eNone);
    EXPECT_TRUE(outgoingMsg.ParseFromArray(receivedMsg.data(), receivedMsg.size()));
    EXPECT_TRUE(outgoingMsg.has_start_provisioning_response());

    // send message to CM
    servicemanager::v4::SMOutgoingMessages smOutgoingMessages;
    smOutgoingMessages.mutable_node_config_status();
    std::vector<uint8_t> messageData2(smOutgoingMessages.ByteSizeLong());
    EXPECT_TRUE(smOutgoingMessages.SerializeToArray(messageData2.data(), messageData2.size()));

    protobufHeader = PrepareProtobufHeader(messageData2.size());
    protobufHeader.insert(protobufHeader.end(), messageData2.begin(), messageData2.end());

    EXPECT_EQ(mCMSecurePipe->Write(protobufHeader), aos::ErrorEnum::eNone);

    auto [receivedMsg2, errReceive2] = CMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive2, aos::ErrorEnum::eNone);

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
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mIAMSecureConnection.Init(mConfig.mIAMConfig.mSecurePort, IAMSecureHandler, *mCommManager,
        &mCertProvider.value(), mConfig.mVChan.mIAMCertStorage);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMSecureConnection.Start(), aos::ErrorEnum::eNone);

    // connect to IAM
    EXPECT_EQ(mIAMSecurePipe->Connect(), aos::ErrorEnum::eNone);

    iamanager::v5::IAMIncomingMessages incomingMsg;
    incomingMsg.mutable_start_provisioning_request();
    std::vector<uint8_t> messageData(incomingMsg.ByteSizeLong());
    EXPECT_TRUE(incomingMsg.SerializeToArray(messageData.data(), messageData.size()));
    EXPECT_EQ(IAMSecureHandler.SetIncomingMessages(messageData), aos::ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mIAMSecurePipe->Read(message), aos::ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mIAMSecurePipe->Read(message), aos::ErrorEnum::eNone);
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
    EXPECT_EQ(mIAMSecurePipe->Write(protobufHeader), aos::ErrorEnum::eNone);

    auto [receivedMsg, errReceive] = IAMSecureHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive, aos::ErrorEnum::eNone);
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
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager, &mDownloader, &mCertProvider.value());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), aos::ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), aos::ErrorEnum::eNone);

    // connect to CM
    EXPECT_EQ(mCMSecurePipe->Connect(), aos::ErrorEnum::eNone);

    servicemanager::v4::SMIncomingMessages incomingMsg;
    incomingMsg.mutable_get_node_config_status();
    std::vector<uint8_t> messageData(incomingMsg.ByteSizeLong());
    EXPECT_TRUE(incomingMsg.SerializeToArray(messageData.data(), messageData.size()));
    EXPECT_EQ(CMHandler.SetIncomingMessages(messageData), aos::ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mCMSecurePipe->Read(message), aos::ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mCMSecurePipe->Read(message), aos::ErrorEnum::eNone);
    servicemanager::v4::SMIncomingMessages incomingMessages;
    EXPECT_TRUE(incomingMessages.ParseFromArray(message.data(), message.size()));
    EXPECT_TRUE(incomingMessages.has_get_node_config_status());

    servicemanager::v4::SMOutgoingMessages smOutgoingMessages;
    smOutgoingMessages.mutable_node_config_status();
    std::vector<uint8_t> messageData2(smOutgoingMessages.ByteSizeLong());
    EXPECT_TRUE(smOutgoingMessages.SerializeToArray(messageData2.data(), messageData2.size()));

    auto protobufHeader = PrepareProtobufHeader(messageData2.size());
    protobufHeader.insert(protobufHeader.end(), messageData2.begin(), messageData2.end());

    EXPECT_EQ(mCMSecurePipe->Write(protobufHeader), aos::ErrorEnum::eNone);

    auto [receivedMsg2, errReceive2] = CMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive2, aos::ErrorEnum::eNone);

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
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager, &mDownloader, &mCertProvider.value());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), aos::ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), aos::ErrorEnum::eNone);

    // connect to CM
    EXPECT_EQ(mCMSecurePipe->Connect(), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mCMSecurePipe->Write(protobufHeader), aos::ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mCMSecurePipe->Read(message), aos::ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mCMSecurePipe->Read(message), aos::ErrorEnum::eNone);
    servicemanager::v4::SMIncomingMessages incomingMessages;
    EXPECT_TRUE(incomingMessages.ParseFromArray(message.data(), message.size()));
    EXPECT_TRUE(incomingMessages.has_image_content_info());

    auto imageCount = incomingMessages.image_content_info().image_files_size();

    EXPECT_EQ(imageCount, 4);

    bool foundService {};
    for (int i = 0; i < imageCount; i++) {
        std::vector<uint8_t> message(sizeof(AosProtobufHeader));
        EXPECT_EQ(mCMSecurePipe->Read(message), aos::ErrorEnum::eNone);
        auto header = ParseProtobufHeader(message);
        message.clear();
        message.resize(header.mDataSize);

        EXPECT_EQ(mCMSecurePipe->Read(message), aos::ErrorEnum::eNone);
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
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager, &mDownloader, &mCertProvider.value());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), aos::ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), aos::ErrorEnum::eNone);

    // connect to CM
    EXPECT_EQ(mCMSecurePipe->Connect(), aos::ErrorEnum::eNone);

    struct LogData {
        std::string                   mLogId;
        std::string                   mLogMessage;
        aos::cloudprotocol::LogStatus mStatus;
    } testLogData[] = {
        {"id1", "test log message1\n", aos::cloudprotocol::LogStatusEnum::eOk},
        {"id1", "test log message2\n", aos::cloudprotocol::LogStatusEnum::eOk},
        {"id1", "", aos::cloudprotocol::LogStatusEnum::eEmpty},
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
        EXPECT_EQ(mCMSecurePipe->Write(protobufHeader), aos::ErrorEnum::eNone);
    }

    auto [receivedMsg, errReceive] = CMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive, aos::ErrorEnum::eNone);

    servicemanager::v4::SMOutgoingMessages outgoingMsg;
    EXPECT_TRUE(outgoingMsg.ParseFromArray(receivedMsg.data(), receivedMsg.size()));

    EXPECT_EQ(outgoingMsg.SMOutgoingMessage_case(), servicemanager::v4::SMOutgoingMessages::kLog);
    EXPECT_EQ(outgoingMsg.log().log_id(), "id1");
    EXPECT_EQ(outgoingMsg.log().status(),
        aos::cloudprotocol::LogStatus(aos::cloudprotocol::LogStatusEnum::eOk).ToString().CStr());

    mCommManager->Stop();
    mCommManagerClient->Close();
    mCMConnection.Stop();
    mCMSecurePipe->Close();
}

TEST_F(CommunicationSecureManagerTest, TestCertChange)
{
    IAMConnection mIAMSecureConnection {};

    mIAMClientChannel = mCommManagerClient->CreateCommChannel(8080);
    mIAMSecurePipe.emplace(*mIAMClientChannel, mKeyURI, mCertPEM, CERTIFICATES_MP_DIR "/ca.cer");

    auto err = mCommManager->Init(mConfig, mServer.value(), &mCertLoader, &mCryptoProvider);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mIAMSecureConnection.Init(mConfig.mIAMConfig.mSecurePort, IAMSecureHandler, *mCommManager,
        &mCertProvider.value(), mConfig.mVChan.mIAMCertStorage);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMSecureConnection.Start(), aos::ErrorEnum::eNone);

    EXPECT_EQ(mIAMSecurePipe->Connect(), aos::ErrorEnum::eNone);

    EXPECT_TRUE(mCertProvider->IsCertCalled());

    mCommManager->OnCertChanged(aos::iam::certhandler::CertInfo {});
    mIAMSecurePipe->Close();
    mCertProvider->ResetCertCalled();

    EXPECT_TRUE(mClient->WaitForConnection());

    EXPECT_EQ(mIAMSecurePipe->Connect(), aos::ErrorEnum::eNone);

    EXPECT_TRUE(mCertProvider->IsCertCalled());

    mCommManager->Stop();
    mCommManagerClient->Close();
    mIAMSecureConnection.Stop();
    mIAMSecurePipe->Close();
}
