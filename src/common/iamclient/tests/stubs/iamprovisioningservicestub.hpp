/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPROVISIONINGSERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPROVISIONINGSERVICESTUB_HPP_

#include <memory>
#include <mutex>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>
#include <iamanager/v6/iamanager.pb.h>

/**
 * Test stub for IAMProvisioningService v6.
 */
class IAMProvisioningServiceStub final : public iamanager::v6::IAMProvisioningService::Service {
public:
    IAMProvisioningServiceStub()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8008", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        mServer = builder.BuildAndStart();
    }

    ~IAMProvisioningServiceStub()
    {
        if (mServer) {
            mServer->Shutdown();
        }
    }

    void SetCertTypes(const std::vector<std::string>& certTypes)
    {
        std::lock_guard lock {mMutex};
        mCertTypes = certTypes;
    }

    void SetProvisioningError(int exitCode, const std::string& message)
    {
        std::lock_guard lock {mMutex};
        mHasError      = true;
        mErrorExitCode = exitCode;
        mErrorMessage  = message;
    }

    void ClearProvisioningError()
    {
        std::lock_guard lock {mMutex};
        mHasError = false;
    }

    std::string GetLastNodeID() const
    {
        std::lock_guard lock {mMutex};
        return mLastNodeID;
    }

    std::string GetLastPassword() const
    {
        std::lock_guard lock {mMutex};
        return mLastPassword;
    }

    grpc::Status GetCertTypes([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::GetCertTypesRequest* request, iamanager::v6::CertTypes* response) override
    {
        std::lock_guard lock {mMutex};

        mLastNodeID = request->node_id();

        for (const auto& certType : mCertTypes) {
            response->add_types(certType);
        }

        return grpc::Status::OK;
    }

    grpc::Status StartProvisioning([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::StartProvisioningRequest*                   request,
        iamanager::v6::StartProvisioningResponse*                        response) override
    {
        std::lock_guard lock {mMutex};

        mLastNodeID   = request->node_id();
        mLastPassword = request->password();

        if (mHasError) {
            response->mutable_error()->set_exit_code(mErrorExitCode);
            response->mutable_error()->set_message(mErrorMessage);
        }

        return grpc::Status::OK;
    }

    grpc::Status FinishProvisioning([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::FinishProvisioningRequest*                   request,
        iamanager::v6::FinishProvisioningResponse*                        response) override
    {
        std::lock_guard lock {mMutex};

        mLastNodeID   = request->node_id();
        mLastPassword = request->password();

        if (mHasError) {
            response->mutable_error()->set_exit_code(mErrorExitCode);
            response->mutable_error()->set_message(mErrorMessage);
        }

        return grpc::Status::OK;
    }

    grpc::Status Deprovision([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::DeprovisionRequest* request, iamanager::v6::DeprovisionResponse* response) override
    {
        std::lock_guard lock {mMutex};

        mLastNodeID   = request->node_id();
        mLastPassword = request->password();

        if (mHasError) {
            response->mutable_error()->set_exit_code(mErrorExitCode);
            response->mutable_error()->set_message(mErrorMessage);
        }

        return grpc::Status::OK;
    }

private:
    std::unique_ptr<grpc::Server> mServer;
    mutable std::mutex            mMutex;
    std::vector<std::string>      mCertTypes;
    bool                          mHasError {false};
    int                           mErrorExitCode {0};
    std::string                   mErrorMessage;
    std::string                   mLastNodeID;
    std::string                   mLastPassword;
};

#endif
