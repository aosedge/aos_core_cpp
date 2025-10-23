/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMNODESSERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMNODESSERVICESTUB_HPP_

#include <memory>
#include <mutex>
#include <string>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>
#include <iamanager/v6/iamanager.pb.h>

/**
 * Test stub for IAMNodesService v6.
 */
class IAMNodesServiceStub final : public iamanager::v6::IAMNodesService::Service {
public:
    IAMNodesServiceStub()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8010", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        mServer = builder.BuildAndStart();
    }

    ~IAMNodesServiceStub()
    {
        if (mServer) {
            mServer->Shutdown();
        }
    }

    void SetError(int exitCode, const std::string& message)
    {
        std::lock_guard lock {mMutex};
        mHasError      = true;
        mErrorExitCode = exitCode;
        mErrorMessage  = message;
    }

    void ClearError()
    {
        std::lock_guard lock {mMutex};
        mHasError = false;
    }

    std::string GetLastNodeID() const
    {
        std::lock_guard lock {mMutex};
        return mLastNodeID;
    }

    grpc::Status PauseNode([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::PauseNodeRequest* request, iamanager::v6::PauseNodeResponse* response) override
    {
        std::lock_guard lock {mMutex};

        mLastNodeID = request->node_id();

        if (mHasError) {
            response->mutable_error()->set_exit_code(mErrorExitCode);
            response->mutable_error()->set_message(mErrorMessage);
        }

        return grpc::Status::OK;
    }

    grpc::Status ResumeNode([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::ResumeNodeRequest* request, iamanager::v6::ResumeNodeResponse* response) override
    {
        std::lock_guard lock {mMutex};

        mLastNodeID = request->node_id();

        if (mHasError) {
            response->mutable_error()->set_exit_code(mErrorExitCode);
            response->mutable_error()->set_message(mErrorMessage);
        }

        return grpc::Status::OK;
    }

private:
    std::unique_ptr<grpc::Server> mServer;
    mutable std::mutex            mMutex;
    bool                          mHasError {false};
    int                           mErrorExitCode {0};
    std::string                   mErrorMessage;
    std::string                   mLastNodeID;
};

#endif
