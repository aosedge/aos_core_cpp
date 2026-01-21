/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>

#include "publicidentityservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

PublicIdentityService::~PublicIdentityService()
{
    if (mSubscriptionManager) {
        mSubscriptionManager->Close();
    }
}

Error PublicIdentityService::Init(
    const std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    LOG_DBG() << "Init public identity service" << Log::Field("iamPublicServerURL", iamPublicServerURL.c_str())
              << Log::Field("insecureConnection", insecureConnection);

    std::lock_guard lock {mMutex};

    mTLSCredentials     = &tlsCredentials;
    mIAMPublicServerURL = iamPublicServerURL;
    mInsecureConnection = insecureConnection;

    if (mInsecureConnection) {
        mCredentials = grpc::InsecureChannelCredentials();
    } else {
        auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials();
        if (!err.IsNone()) {
            return err;
        }

        mCredentials = credentials;
    }

    mStub = iamanager::v6::IAMPublicIdentityService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicIdentityService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect public identity service";

    auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials();
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMPublicIdentityService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    if (mSubscriptionManager) {
        mSubscriptionManager->Reconnect(mStub.get());
    }

    return ErrorEnum::eNone;
}

Error PublicIdentityService::GetSystemInfo(SystemInfo& info)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get system info";

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    google::protobuf::Empty   request;
    iamanager::v6::SystemInfo response;

    if (auto status = mStub->GetSystemInfo(ctx.get(), request, &response); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    info.mSystemID  = response.system_id().c_str();
    info.mUnitModel = response.unit_model().c_str();
    info.mVersion   = response.version().c_str();

    LOG_DBG() << "System info received" << Log::Field("systemID", info.mSystemID)
              << Log::Field("unitModel", info.mUnitModel) << Log::Field("version", info.mVersion);

    return ErrorEnum::eNone;
}

Error PublicIdentityService::GetSubjects(Array<StaticString<cIDLen>>& subjects)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get subjects";

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    google::protobuf::Empty request;
    iamanager::v6::Subjects response;

    if (auto status = mStub->GetSubjects(ctx.get(), request, &response); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    for (const auto& subject : response.subjects()) {
        if (auto err = subjects.EmplaceBack(subject.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    LOG_DBG() << "Subjects received" << Log::Field("count", subjects.Size());

    return ErrorEnum::eNone;
}

Error PublicIdentityService::SubscribeListener(aos::iamclient::SubjectsListenerItf& subjectsListener)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Subscribe to subjects changed";

    if (!mSubscriptionManager) {
        google::protobuf::Empty request;

        auto convertFunc = [](const iamanager::v6::Subjects& proto, SubjectArray& subjects) -> Error {
            for (const auto& subject : proto.subjects()) {
                if (auto err = subjects.EmplaceBack(subject.c_str()); !err.IsNone()) {
                    return AOS_ERROR_WRAP(err);
                }
            }
            return ErrorEnum::eNone;
        };

        auto notifyFunc = [](aos::iamclient::SubjectsListenerItf& listener, const SubjectArray& subjects) {
            listener.SubjectsChanged(subjects);
        };

        mSubscriptionManager = std::make_unique<SubjectsSubscriptionManager>(mStub.get(), request,
            &iamanager::v6::IAMPublicIdentityService::Stub::SubscribeSubjectsChanged, convertFunc, notifyFunc,
            "SubjectsSubscription");
    }

    return mSubscriptionManager->Subscribe(subjectsListener);
}

Error PublicIdentityService::UnsubscribeListener(aos::iamclient::SubjectsListenerItf& subjectsListener)
{
    std::lock_guard lock {mMutex};

    if (!mSubscriptionManager) {
        return ErrorEnum::eNone;
    }

    LOG_INF() << "Unsubscribe from subjects changed";

    if (mSubscriptionManager->Unsubscribe(subjectsListener)) {
        mSubscriptionManager.reset();
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::iamclient
