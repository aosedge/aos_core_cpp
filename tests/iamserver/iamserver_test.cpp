/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENSSL_SUPPRESS_DEPRECATED
// Suppress deprecated warnings from OpenSSL to use ENGINE_* functions
#define OPENSSL_SUPPRESS_DEPRECATED
#endif

#include <gmock/gmock.h>
#include <openssl/engine.h>

#include <test/utils/log.hpp>
#include <test/utils/softhsmenv.hpp>

#include <aos/common/crypto/mbedtls/cryptoprovider.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/certmodules/pkcs11/pkcs11.hpp>
#include <utils/grpchelper.hpp>

#include "iamserver/iamserver.hpp"

#include "mocks/identhandlermock.hpp"
#include "mocks/nodeinfoprovidermock.hpp"
#include "mocks/nodemanagermock.hpp"
#include "mocks/permissionhandlermock.hpp"
#include "mocks/provisionmanagermock.hpp"
#include "stubs/storagestub.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class IAMServerTest : public Test {
protected:
    // Default parameters
    static constexpr auto cPIN                 = "admin";
    static constexpr auto cLabel               = "iam-server-test-slot";
    static constexpr auto cMaxModulesCount     = 3;
    static constexpr auto cSystemID            = "system-id";
    static constexpr auto cUnitModel           = "unit-model";
    static constexpr auto cProvisioningModeOn  = true;
    static constexpr auto cProvisioningModeOff = false;

    void RegisterPKCS11Module(const aos::String& name, aos::crypto::KeyType keyType = aos::crypto::KeyTypeEnum::eRSA);
    void SetUpCertificates();

    template <typename T>
    std::unique_ptr<typename T::Stub> CreateCustomStub(const std::string& url, const bool insecure = false)
    {
        auto tlsChannelCreds = insecure
            ? grpc::InsecureChannelCredentials()
            : aos::common::utils::GetTLSClientCredentials(GetClientConfig().mCACert.c_str());
        if (tlsChannelCreds == nullptr) {
            return nullptr;
        }

        auto channel = grpc::CreateCustomChannel(url, tlsChannelCreds, grpc::ChannelArguments());
        if (channel == nullptr) {
            return nullptr;
        }

        return T::NewStub(channel);
    }

    IAMServer                       mServer;
    aos::iam::certhandler::CertInfo mClientInfo;
    aos::iam::certhandler::CertInfo mServerInfo;
    Config                          mServerConfig;
    Config                          mClientConfig;

    aos::iam::certhandler::CertHandler mCertHandler;
    aos::crypto::MbedTLSCryptoProvider mCryptoProvider;
    aos::cryptoutils::CertLoader       mCertLoader;

    // mocks
    aos::iam::identhandler::IdentHandlerMock         mIdentHandler;
    aos::iam::permhandler::PermHandlerMock           mPermHandler;
    NodeInfoProviderMock                             mNodeInfoProvider;
    NodeManagerMock                                  mNodeManager;
    aos::iam::provisionmanager::ProvisionManagerMock mProvisionManager;

private:
    void SetUp() override;
    void TearDown() override;

    // CertHandler function
    aos::iam::certhandler::ModuleConfig       GetCertModuleConfig(aos::crypto::KeyType keyType);
    aos::iam::certhandler::PKCS11ModuleConfig GetPKCS11ModuleConfig();
    void ApplyCertificate(const aos::String& certType, const aos::String& subject, const aos::String& intermKeyPath,
        const aos::String& intermCertPath, uint64_t serial, aos::iam::certhandler::CertInfo& certInfo);

    Config GetServerConfig();
    Config GetClientConfig();

    aos::test::SoftHSMEnv                                                   mSOFTHSMEnv;
    aos::iam::certhandler::StorageStub                                      mStorage;
    aos::StaticArray<aos::iam::certhandler::PKCS11Module, cMaxModulesCount> mPKCS11Modules;
    aos::StaticArray<aos::iam::certhandler::CertModule, cMaxModulesCount>   mCertModules;
};

void IAMServerTest::SetUp()
{
    aos::InitLogs();

    ASSERT_TRUE(mCryptoProvider.Init().IsNone());
    ASSERT_TRUE(mSOFTHSMEnv
                    .Init("", "certhandler-integration-tests", SOFTHSM_BASE_DIR "/softhsm2.conf",
                        SOFTHSM_BASE_DIR "/tokens", SOFTHSM2_LIB)
                    .IsNone());
    ASSERT_TRUE(mCertLoader.Init(mCryptoProvider, mSOFTHSMEnv.GetManager()).IsNone());

    RegisterPKCS11Module("client");
    ASSERT_TRUE(mCertHandler.SetOwner("client", cPIN).IsNone());

    RegisterPKCS11Module("server");

    ApplyCertificate("client", "client", CERTIFICATES_DIR "/client_int.key", CERTIFICATES_DIR "/client_int.cer",
        0x3333444, mClientInfo);
    ApplyCertificate("server", "localhost", CERTIFICATES_DIR "/server_int.key", CERTIFICATES_DIR "/server_int.cer",
        0x3333333, mServerInfo);

    mServerConfig = GetServerConfig();
    mClientConfig = GetClientConfig();

    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillRepeatedly(Invoke([&](aos::NodeInfo& nodeInfo) {
        nodeInfo.mNodeID   = "node0";
        nodeInfo.mNodeType = mServerConfig.mNodeInfo.mNodeType.c_str();

        nodeInfo.mNodeType = mServerConfig.mNodeInfo.mNodeType.c_str();
        nodeInfo.mAttrs.PushBack({"NODE_TYPE", "main"});

        LOG_DBG() << "NodeInfoProvider::GetNodeInfo: " << nodeInfo.mNodeID.CStr() << ", " << nodeInfo.mNodeType.CStr();

        return aos::ErrorEnum::eNone;
    }));
}

void IAMServerTest::TearDown()
{
    if (auto engine = ENGINE_by_id("pkcs11"); engine != nullptr) {
        // Clear the PKCS#11 engine cache like slots/sessions
        ENGINE_get_finish_function(engine)(engine);
    }

    aos::FS::ClearDir(SOFTHSM_BASE_DIR "/tokens");
}

void IAMServerTest::RegisterPKCS11Module(const aos::String& name, aos::crypto::KeyType keyType)
{
    ASSERT_TRUE(mPKCS11Modules.EmplaceBack().IsNone());
    ASSERT_TRUE(mCertModules.EmplaceBack().IsNone());
    auto& pkcs11Module = mPKCS11Modules.Back().mValue;
    auto& certModule   = mCertModules.Back().mValue;
    ASSERT_TRUE(pkcs11Module.Init(name, GetPKCS11ModuleConfig(), mSOFTHSMEnv.GetManager(), mCryptoProvider).IsNone());
    ASSERT_TRUE(certModule.Init(name, GetCertModuleConfig(keyType), mCryptoProvider, pkcs11Module, mStorage).IsNone());
    ASSERT_TRUE(mCertHandler.RegisterModule(certModule).IsNone());
}

Config IAMServerTest::GetServerConfig()
{
    Config config;

    config.mCertStorage               = "server";
    config.mCACert                    = CERTIFICATES_DIR "/ca.cer";
    config.mIAMPublicServerURL        = "localhost:8088";
    config.mIAMProtectedServerURL     = "localhost:8089";
    config.mNodeInfo.mNodeIDPath      = "nodeid";
    config.mNodeInfo.mNodeType        = "iam-node-type";
    config.mFinishProvisioningCmdArgs = config.mDiskEncryptionCmdArgs = {};

    return config;
}

Config IAMServerTest::GetClientConfig()
{
    Config config;

    config.mCertStorage               = "client";
    config.mCACert                    = CERTIFICATES_DIR "/ca.cer";
    config.mIAMPublicServerURL        = "localhost:8088";
    config.mIAMProtectedServerURL     = "localhost:8089";
    config.mNodeInfo.mNodeType        = "iam-node-type";
    config.mFinishProvisioningCmdArgs = config.mDiskEncryptionCmdArgs = {};

    return config;
}

aos::iam::certhandler::ModuleConfig IAMServerTest::GetCertModuleConfig(aos::crypto::KeyType keyType)
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

aos::iam::certhandler::PKCS11ModuleConfig IAMServerTest::GetPKCS11ModuleConfig()
{
    aos::iam::certhandler::PKCS11ModuleConfig config;

    config.mLibrary         = SOFTHSM2_LIB;
    config.mSlotID          = mSOFTHSMEnv.GetSlotID();
    config.mUserPINPath     = CERTIFICATES_DIR "/pin.txt";
    config.mModulePathInURL = true;

    return config;
}

void IAMServerTest::ApplyCertificate(const aos::String& certType, const aos::String& subject,
    const aos::String& intermKeyPath, const aos::String& intermCertPath, uint64_t serial,
    aos::iam::certhandler::CertInfo& certInfo)
{
    aos::StaticString<aos::crypto::cCSRPEMLen> csr;
    ASSERT_TRUE(mCertHandler.CreateKey(certType, subject, cPIN, csr).IsNone());

    // create certificate from CSR, CA priv key, CA cert
    aos::StaticString<aos::crypto::cPrivKeyPEMLen> intermKey;
    ASSERT_TRUE(aos::FS::ReadFileToString(intermKeyPath, intermKey).IsNone());

    aos::StaticString<aos::crypto::cCertPEMLen> intermCert;
    ASSERT_TRUE(aos::FS::ReadFileToString(intermCertPath, intermCert).IsNone());

    auto serialArr = aos::Array<uint8_t>(reinterpret_cast<uint8_t*>(&serial), sizeof(serial));
    aos::StaticString<aos::crypto::cCertPEMLen> clientCertChain;

    ASSERT_TRUE(mCryptoProvider.CreateClientCert(csr, intermKey, intermCert, serialArr, clientCertChain).IsNone());

    // add intermediate cert to the chain
    clientCertChain.Append(intermCert);

    // add CA certificate to the chain
    aos::StaticString<aos::crypto::cCertPEMLen> caCert;

    ASSERT_TRUE(aos::FS::ReadFileToString(CERTIFICATES_DIR "/ca.cer", caCert).IsNone());
    clientCertChain.Append(caCert);

    // apply client certificate
    // FS::WriteStringToFile(CERTIFICATES_DIR "/client-out.pem", clientCertChain, 0666);
    ASSERT_TRUE(mCertHandler.ApplyCertificate(certType, clientCertChain, certInfo).IsNone());
    EXPECT_EQ(certInfo.mSerial, serialArr);
}

/***********************************************************************************************************************
 * IAMServer tests
 **********************************************************************************************************************/

TEST_F(IAMServerTest, InitFailsOnHandlersInit)
{
    // public message handler initialization fails
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(Return(aos::ErrorEnum::eFailed));
    EXPECT_CALL(mNodeManager, SetNodeInfo).Times(0);

    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOn);
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed)) << err.Message();
}

TEST_F(IAMServerTest, InitWithInsecureChannelsSucceeds)
{
    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOn);
    ASSERT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(IAMServerTest, InitWithSecureChannelsSucceeds)
{
    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOff);
    ASSERT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(IAMServerTest, InitWithSecureChannelsFails)
{
    mServerConfig.mCertStorage = "unknown";

    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOff);
    ASSERT_FALSE(err.IsNone());
}

TEST_F(IAMServerTest, OnNodeInfoChange)
{
    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOn);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    aos::NodeInfo nodeInfo;

    ASSERT_NO_THROW(mServer.OnNodeInfoChange(nodeInfo));
}

TEST_F(IAMServerTest, PublicIdentityServiceIsNotImplementedOnSecondaryNode)
{
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillRepeatedly(Invoke([&](aos::NodeInfo& nodeInfo) {
        nodeInfo.mNodeID   = "node0";
        nodeInfo.mNodeType = mServerConfig.mNodeInfo.mNodeType.c_str();
        nodeInfo.mAttrs.PushBack({"NODE_TYPE", "secondary"});

        return aos::ErrorEnum::eNone;
    }));

    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOn);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto stub = CreateCustomStub<iamproto::IAMPublicIdentityService>(
        mServerConfig.mIAMProtectedServerURL, cProvisioningModeOn);

    EXPECT_NE(stub, nullptr) << "Failed to create a stub";

    grpc::ClientContext  context;
    iamproto::SystemInfo response;

    auto status = stub->GetSystemInfo(&context, {}, &response);

    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNIMPLEMENTED)
        << "IAMPublicIdentityService must be unimplemented: code = " << status.error_code()
        << ", message = " << status.error_message();
}

TEST_F(IAMServerTest, PublicNodesServiceIsNotImplementedOnSecondaryNode)
{
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillRepeatedly(Invoke([&](aos::NodeInfo& nodeInfo) {
        nodeInfo.mNodeID   = "node0";
        nodeInfo.mNodeType = mServerConfig.mNodeInfo.mNodeType.c_str();
        nodeInfo.mAttrs.PushBack({"NODE_TYPE", "secondary"});

        return aos::ErrorEnum::eNone;
    }));

    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOn);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto stub
        = CreateCustomStub<iamproto::IAMPublicNodesService>(mServerConfig.mIAMProtectedServerURL, cProvisioningModeOn);

    EXPECT_NE(stub, nullptr) << "Failed to create a stub";

    grpc::ClientContext context;
    iamproto::NodesID   response;

    auto status = stub->GetAllNodeIDs(&context, {}, &response);

    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNIMPLEMENTED)
        << "IAMPublicNodesService must be unimplemented: code = " << status.error_code()
        << ", message = " << status.error_message();
}

TEST_F(IAMServerTest, CertificateServiceIsNotImplementedOnSecondaryNode)
{
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillRepeatedly(Invoke([&](aos::NodeInfo& nodeInfo) {
        nodeInfo.mNodeID   = "node0";
        nodeInfo.mNodeType = mServerConfig.mNodeInfo.mNodeType.c_str();
        nodeInfo.mAttrs.PushBack({"node_type", "secondary"});

        return aos::ErrorEnum::eNone;
    }));

    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOn);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto stub
        = CreateCustomStub<iamproto::IAMCertificateService>(mServerConfig.mIAMProtectedServerURL, cProvisioningModeOn);

    EXPECT_NE(stub, nullptr) << "Failed to create a stub";

    grpc::ClientContext         context;
    iamproto::CreateKeyRequest  request;
    iamproto::CreateKeyResponse response;

    auto status = stub->CreateKey(&context, request, &response);

    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNIMPLEMENTED)
        << "IAMCertificateService must be unimplemented: code = " << status.error_code()
        << ", message = " << status.error_message();
}

TEST_F(IAMServerTest, ProvisioningServiceIsNotImplementedOnSecondaryNode)
{
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillRepeatedly(Invoke([&](aos::NodeInfo& nodeInfo) {
        nodeInfo.mNodeID   = "node0";
        nodeInfo.mNodeType = mServerConfig.mNodeInfo.mNodeType.c_str();
        nodeInfo.mAttrs.PushBack({"NODE_TYPE", "secondary"});

        return aos::ErrorEnum::eNone;
    }));

    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOn);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto stub
        = CreateCustomStub<iamproto::IAMProvisioningService>(mServerConfig.mIAMProtectedServerURL, cProvisioningModeOn);

    EXPECT_NE(stub, nullptr) << "Failed to create a stub";

    grpc::ClientContext           context;
    iamproto::GetCertTypesRequest request;
    iamproto::CertTypes           response;

    auto status = stub->GetCertTypes(&context, request, &response);

    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNIMPLEMENTED)
        << "IAMProvisioningService must be unimplemented: code = " << status.error_code()
        << ", message = " << status.error_message();
}

TEST_F(IAMServerTest, NodesServiceIsNotImplementedOnSecondaryNode)
{
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillRepeatedly(Invoke([&](aos::NodeInfo& nodeInfo) {
        nodeInfo.mNodeID   = "node0";
        nodeInfo.mNodeType = mServerConfig.mNodeInfo.mNodeType.c_str();
        nodeInfo.mAttrs.PushBack({"NODE_TYPE", "secondary"});

        return aos::ErrorEnum::eNone;
    }));

    auto err = mServer.Init(mServerConfig, mCertHandler, mIdentHandler, mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mProvisionManager, cProvisioningModeOn);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto stub = CreateCustomStub<iamproto::IAMNodesService>(mServerConfig.mIAMProtectedServerURL, cProvisioningModeOn);

    EXPECT_NE(stub, nullptr) << "Failed to create a stub";

    grpc::ClientContext         context;
    iamproto::PauseNodeRequest  request;
    iamproto::PauseNodeResponse response;

    auto status = stub->PauseNode(&context, request, &response);

    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNIMPLEMENTED)
        << "IAMNodesService must be unimplemented: code = " << status.error_code()
        << ", message = " << status.error_message();
}
