/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/logger/logmodule.hpp>
#include <common/pbconvert/common.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/grpchelper.hpp>

#include "publicidentityhandler.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error PublicIdentityServiceHandler::Init(
    const std::string& serviceURL, const std::string& certStorage, TLSCredentialsItf& TLSCredentials)
{
    LOG_DBG() << "Init public identity service handler" << Log::Field("url", serviceURL.c_str())
              << Log::Field("certStorage", certStorage.c_str());

    mCertStorage    = certStorage;
    mTLSCredentials = &TLSCredentials;
    mServiceURL     = serviceURL;

    return ErrorEnum::eNone;
}

Error PublicIdentityServiceHandler::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start public identity service handler";

    if (mIsRunning) {
        return ErrorEnum::eWrongState;
    }

    mIsRunning = true;

    mThread = std::thread(&PublicIdentityServiceHandler::Run, this);

    return ErrorEnum::eNone;
}

Error PublicIdentityServiceHandler::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Stop public identity service handler";

        if (!mIsRunning) {
            return ErrorEnum::eWrongState;
        }

        mIsRunning = false;

        if (mSubjectsChangedContext) {
            mSubjectsChangedContext->TryCancel();
        }
    }

    if (mThread.joinable()) {
        mThread.join();
    }

    return ErrorEnum::eNone;
}

RetWithError<StaticString<cIDLen>> PublicIdentityServiceHandler::GetSystemID()
{
    LOG_INF() << "Get system ID";

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cRPCCallTimeout);

        auto stub = iamanager::v5::IAMPublicIdentityService::NewStub(
            grpc::CreateCustomChannel(mServiceURL, CreateCredentials(), grpc::ChannelArguments()));

        google::protobuf::Empty   request;
        iamanager::v5::SystemInfo response;

        if (auto status = stub->GetSystemInfo(ctx.get(), request, &response); !status.ok()) {
            return {{}, Error(ErrorEnum::eRuntime, status.error_message().c_str())};
        }

        StaticString<cIDLen> systemID;

        if (auto err = systemID.Assign(response.system_id().c_str()); !err.IsNone()) {
            return {{}, AOS_ERROR_WRAP(err)};
        }

        return systemID;
    } catch (const std::exception& e) {
        return {{}, AOS_ERROR_WRAP(utils::ToAosError(e))};
    }

    return {{}, ErrorEnum::eFailed};
}

RetWithError<StaticString<cUnitModelLen>> PublicIdentityServiceHandler::GetUnitModel()
{
    LOG_INF() << "Get unit model";

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cRPCCallTimeout);

        auto stub = iamanager::v5::IAMPublicIdentityService::NewStub(
            grpc::CreateCustomChannel(mServiceURL, CreateCredentials(), grpc::ChannelArguments()));

        google::protobuf::Empty   request;
        iamanager::v5::SystemInfo response;

        if (auto status = stub->GetSystemInfo(ctx.get(), request, &response); !status.ok()) {
            return {{}, Error(ErrorEnum::eRuntime, status.error_message().c_str())};
        }

        StaticString<cUnitModelLen> unitModel;

        if (auto err = unitModel.Assign(response.unit_model().c_str()); !err.IsNone()) {
            return {{}, AOS_ERROR_WRAP(err)};
        }

        return unitModel;
    } catch (const std::exception& e) {
        return {{}, AOS_ERROR_WRAP(utils::ToAosError(e))};
    }

    return {{}, ErrorEnum::eFailed};
}

Error PublicIdentityServiceHandler::GetSubjects(Array<StaticString<cIDLen>>& subjects)
{
    LOG_INF() << "Get subjects";

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cRPCCallTimeout);

        auto stub = iamanager::v5::IAMPublicIdentityService::NewStub(
            grpc::CreateCustomChannel(mServiceURL, CreateCredentials(), grpc::ChannelArguments()));

        google::protobuf::Empty request;
        iamanager::v5::Subjects response;

        if (auto status = stub->GetSubjects(ctx.get(), request, &response); !status.ok()) {
            return Error(ErrorEnum::eRuntime, status.error_message().c_str());
        }

        for (const auto& subject : response.subjects()) {
            if (auto err = subjects.EmplaceBack(); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            if (auto err = subjects.Back().Assign(subject.c_str()); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eFailed;
}

Error PublicIdentityServiceHandler::SubscribeSubjectsChanged(identprovider::SubjectsObserverItf& observer)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Subscribe to subjects changed";

    if (std::find(mObservers.begin(), mObservers.end(), &observer) != mObservers.end()) {
        return ErrorEnum::eAlreadyExist;
    }

    mObservers.push_back(&observer);

    return ErrorEnum::eNone;
}

void PublicIdentityServiceHandler::UnsubscribeSubjectsChanged(identprovider::SubjectsObserverItf& observer)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Unsubscribe from subjects changed";

    mObservers.erase(std::remove(mObservers.begin(), mObservers.end(), &observer), mObservers.end());
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void PublicIdentityServiceHandler::Run()
{
    LOG_INF() << "Public identity service handler thread started";

    while (true) {
        {
            std::lock_guard lock {mMutex};

            if (!mIsRunning) {
                break;
            }
        }

        ReceiveChangedSubjects();
    }

    LOG_INF() << "Public identity service handler thread stopped";
}

void PublicIdentityServiceHandler::ReceiveChangedSubjects()
{
    LOG_DBG() << "Receive subjects changed";

    try {
        std::unique_ptr<iamanager::v5::IAMPublicIdentityService::Stub>   stub;
        std::unique_ptr<::grpc::ClientReader<::iamanager::v5::Subjects>> stream;
        google::protobuf::Empty                                          request;
        iamanager::v5::Subjects                                          changedSubjects;
        auto subjects = std::make_unique<StaticArray<StaticString<cIDLen>, cMaxNumSubjects>>();

        {
            std::lock_guard lock {mMutex};

            mSubjectsChangedContext = std::make_unique<grpc::ClientContext>();
            mSubjectsChangedContext->set_deadline(std::chrono::system_clock::now() + cRPCCallTimeout);

            stub = iamanager::v5::IAMPublicIdentityService::NewStub(
                grpc::CreateCustomChannel(mServiceURL, CreateCredentials(), grpc::ChannelArguments()));

            stream = stub->SubscribeSubjectsChanged(mSubjectsChangedContext.get(), request);
        }

        while (stream->Read(&changedSubjects)) {
            subjects->Clear();

            for (const auto& subject : changedSubjects.subjects()) {
                if (auto err = subjects->EmplaceBack(); !err.IsNone()) {
                    LOG_ERR() << "Failed to handle changed subject" << Log::Field(AOS_ERROR_WRAP(err));
                    continue;
                }

                if (auto err = subjects->Back().Assign(subject.c_str()); !err.IsNone()) {
                    LOG_ERR() << "Failed to handle changed subject" << Log::Field(AOS_ERROR_WRAP(err));
                    continue;
                }
            }

            NotifyObservers(*subjects);
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Receive changed subject failed" << Log::Field(utils::ToAosError(e));
    }
}

void PublicIdentityServiceHandler::NotifyObservers(const Array<StaticString<cIDLen>>& subjects)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Notify observers about subjects changed" << Log::Field("subjectsCount", subjects.Size());

    for (const auto observer : mObservers) {
        if (observer) {
            observer->SubjectsChanged(subjects);
        }
    }
}

std::shared_ptr<grpc::ChannelCredentials> PublicIdentityServiceHandler::CreateCredentials()
{
    iam::certhandler::CertInfo certInfo;

    auto [credential, err] = mTLSCredentials->GetMTLSClientCredentials(mCertStorage.c_str());
    if (!err.IsNone()) {
        throw std::runtime_error("failed to get MTLS config");
    }

    return credential;
}

} // namespace aos::common::iamclient
