/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/mocks/identprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/iam/tests/mocks/certhandlermock.hpp>

#include <common/iamclient/permservicehandler.hpp>
#include <common/iamclient/publicidentityhandler.hpp>
#include <common/iamclient/publicservicehandler.hpp>
#include <common/tests/mocks/iamclientmock.hpp>

#include "stubs/iamserverstub.hpp"

using namespace testing;
using namespace aos::common::iamclient;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class IamClientTest : public Test {
public:
    IamClientTest() { mConfig.mIAMPublicServerURL = "localhost:8002"; }

protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mIAMServerStub.emplace();
        mClient.emplace();
        mPermServiceHandler.emplace();
        mPublicIdentityServiceHandler.emplace();

        auto getMTLSCredentials = [this](const aos::iam::certhandler::CertInfo& certInfo, const aos::String&,
                                      aos::crypto::CertLoaderItf&, aos::crypto::x509::ProviderItf&) {
            mCertInfo = certInfo;

            return nullptr;
        };

        auto err = mClient->Init(mConfig, *mCertLoader, *mCryptoProvider, true, std::move(getMTLSCredentials));

        ASSERT_EQ(err, aos::ErrorEnum::eNone);

        err = mPermServiceHandler->Init("localhost:8002", "cert_storage", mTLSCredentialsMock);
        ASSERT_EQ(err, aos::ErrorEnum::eNone) << aos::tests::utils::ErrorToStr(err);

        err = mPublicIdentityServiceHandler->Init("localhost:8002", "cert_storage", mTLSCredentialsMock);
        ASSERT_EQ(err, aos::ErrorEnum::eNone) << aos::tests::utils::ErrorToStr(err);
    }

    std::optional<TestIAMServerStub> mIAMServerStub;

    std::optional<PublicServiceHandler>         mClient;
    std::optional<PermissionsServiceHandler>    mPermServiceHandler;
    std::optional<PublicIdentityServiceHandler> mPublicIdentityServiceHandler;
    aos::crypto::CertLoaderItf*                 mCertLoader {};
    aos::crypto::x509::ProviderItf*             mCryptoProvider {};
    TLSCredentialsMock                          mTLSCredentialsMock {};
    Config                                      mConfig {};
    aos::iam::certhandler::CertInfo             mCertInfo {};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(IamClientTest, GetClientMTLSConfig)
{
    aos::iam::certhandler::CertInfo certInfo;
    certInfo.mCertURL = "client_cert";
    certInfo.mKeyURL  = "client_key";

    mIAMServerStub->SetCertInfo(certInfo);

    auto [_, err] = mClient->GetMTLSClientCredentials("client_cert_type");

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMServerStub->GetCertType(), "client_cert_type");
    EXPECT_EQ(mCertInfo, certInfo);
}

TEST_F(IamClientTest, GetCertificate)
{
    aos::iam::certhandler::CertInfo certInfo;
    certInfo.mCertURL = "client_cert";
    certInfo.mKeyURL  = "client_key";

    mIAMServerStub->SetCertInfo(certInfo);

    aos::iam::certhandler::CertInfo requestCertInfo;

    auto err = mClient->GetCert("client_cert_type", {}, {}, requestCertInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMServerStub->GetCertType(), "client_cert_type");
    EXPECT_EQ(certInfo, requestCertInfo);
}

TEST_F(IamClientTest, GetNodeInfo)
{
    aos::NodeInfoObsolete nodeInfo;

    auto err = mClient->GetNodeInfo(nodeInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node_id");
    EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "node_type");
    EXPECT_STREQ(nodeInfo.mName.CStr(), "name");
    EXPECT_EQ(nodeInfo.mState, aos::NodeStateObsoleteEnum::eProvisioned);
    EXPECT_STREQ(nodeInfo.mOSType.CStr(), "os_type");

    EXPECT_EQ(nodeInfo.mCPUs.Size(), 1);
    EXPECT_STREQ(nodeInfo.mCPUs[0].mModelName.CStr(), "model_name");
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumCores, 1);
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumThreads, 1);
    EXPECT_STREQ(nodeInfo.mCPUs[0].mArchInfo.mArchitecture.CStr(), "arch");
    EXPECT_STREQ(nodeInfo.mCPUs[0].mArchInfo.mVariant->CStr(), "arch_family");
    EXPECT_EQ(nodeInfo.mCPUs[0].mMaxDMIPS, 1);

    EXPECT_EQ(nodeInfo.mMaxDMIPS, 1);
    EXPECT_EQ(nodeInfo.mTotalRAM, 1);

    EXPECT_EQ(nodeInfo.mPartitions.Size(), 1);
    EXPECT_STREQ(nodeInfo.mPartitions[0].mName.CStr(), "name");
    EXPECT_EQ(nodeInfo.mPartitions[0].mTypes.Size(), 1);
    EXPECT_STREQ(nodeInfo.mPartitions[0].mTypes[0].CStr(), "types");
    EXPECT_EQ(nodeInfo.mPartitions[0].mTotalSize, 1);
    EXPECT_STREQ(nodeInfo.mPartitions[0].mPath.CStr(), "path");

    EXPECT_EQ(nodeInfo.mAttrs.Size(), 1);
    EXPECT_STREQ(nodeInfo.mAttrs[0].mName.CStr(), "name");
    EXPECT_STREQ(nodeInfo.mAttrs[0].mValue.CStr(), "value");
}

TEST_F(IamClientTest, SubscribeCertChangedAndGetCertificate_MultiSubscription)
{
    aos::iam::certhandler::CertReceiverMock subscriber1;
    aos::iam::certhandler::CertReceiverMock subscriber2;

    iamanager::v5::CertInfo certInfo;
    certInfo.set_type("client_cert_type");
    certInfo.set_cert_url("client_cert");
    certInfo.set_key_url("client_key");

    mClient->SubscribeCertChanged("client_cert_type", subscriber1);
    mClient->SubscribeCertChanged("client_cert_type", subscriber2);

    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    mIAMServerStub->SendCertChangedInfo(certInfo);

    EXPECT_CALL(subscriber1, OnCertChanged(_)).Times(1);
    EXPECT_CALL(subscriber2, OnCertChanged(_)).Times(1);

    aos::iam::certhandler::CertInfo requestCertInfo;

    auto err = mClient->GetCert("client_cert_type", {}, {}, requestCertInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(requestCertInfo.mCertURL, "client_cert");
    EXPECT_EQ(requestCertInfo.mKeyURL, "client_key");

    mClient->UnsubscribeCertChanged(subscriber1);

    mIAMServerStub->SendCertChangedInfo(certInfo);

    EXPECT_CALL(subscriber1, OnCertChanged(_)).Times(0);
    EXPECT_CALL(subscriber2, OnCertChanged(_)).Times(1);

    err = mClient->GetCert("client_cert_type", {}, {}, requestCertInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(requestCertInfo.mCertURL, "client_cert");
    EXPECT_EQ(requestCertInfo.mKeyURL, "client_key");

    mClient->UnsubscribeCertChanged(subscriber2);
    mIAMServerStub->Close();
}

TEST_F(IamClientTest, RegisterUnregisterInstance)
{
    aos::InstanceIdent instanceIdent;

    instanceIdent.mItemID    = "service_id";
    instanceIdent.mSubjectID = "subject_id";
    instanceIdent.mInstance  = 1;

    aos::StaticArray<aos::FunctionServicePermissions, 1> instancePermissions;

    EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_))
        .Times(2)
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

    auto [secret, err] = mPermServiceHandler->RegisterInstance(instanceIdent, instancePermissions);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_STREQ(secret.CStr(), "secret");

    err = mPermServiceHandler->UnregisterInstance(instanceIdent);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    mIAMServerStub->Close();
}

TEST_F(IamClientTest, GetSystemID)
{
    EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_))
        .Times(1)
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

    auto [systemID, err] = mPublicIdentityServiceHandler->GetSystemID();

    EXPECT_EQ(err, aos::ErrorEnum::eNone) << aos::tests::utils::ErrorToStr(err);

    EXPECT_STREQ(systemID.CStr(), "system_id");

    mIAMServerStub->Close();
}

TEST_F(IamClientTest, GetUnitModel)
{
    EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_))
        .Times(1)
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

    auto [unitModel, err] = mPublicIdentityServiceHandler->GetUnitModel();

    EXPECT_EQ(err, aos::ErrorEnum::eNone) << aos::tests::utils::ErrorToStr(err);

    EXPECT_STREQ(unitModel.CStr(), "unit_model");

    mIAMServerStub->Close();
}

TEST_F(IamClientTest, GetSubjects)
{
    EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_))
        .Times(1)
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

    aos::StaticArray<aos::StaticString<aos::cIDLen>, 2> subjects;

    auto err = mPublicIdentityServiceHandler->GetSubjects(subjects);

    EXPECT_EQ(err, aos::ErrorEnum::eNone) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(subjects.Size(), 2);

    EXPECT_STREQ(subjects[0].CStr(), "subject1");
    EXPECT_STREQ(subjects[1].CStr(), "subject2");

    mIAMServerStub->Close();
}

TEST_F(IamClientTest, SubscribeSubjectsChanged)
{
    aos::identprovider::SubjectsObserverMock observer;
    std::promise<void>                       subjectsPromise;

    EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_))
        .Times(1)
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

    aos::StaticArray<aos::StaticString<aos::cIDLen>, 2> subjects;
    subjects.EmplaceBack("subject1");
    subjects.EmplaceBack("subject2");

    auto err = mPublicIdentityServiceHandler->Start();
    EXPECT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    err = mPublicIdentityServiceHandler->SubscribeSubjectsChanged(observer);
    EXPECT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    EXPECT_CALL(observer, SubjectsChanged(subjects)).WillOnce(Invoke([&subjectsPromise](const auto&) {
        subjectsPromise.set_value();

        return aos::Error(aos::ErrorEnum::eNone);
    }));

    EXPECT_TRUE(mIAMServerStub->WaitForConnection());
    mIAMServerStub->SendSubjectsChanged({subjects[0].CStr(), subjects[1].CStr()});

    auto future = subjectsPromise.get_future();
    EXPECT_TRUE(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

    err = mPublicIdentityServiceHandler->Stop();
    EXPECT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    mIAMServerStub->Close();
}
