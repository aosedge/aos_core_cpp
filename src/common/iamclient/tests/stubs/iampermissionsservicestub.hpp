/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPERMISSIONSSERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPERMISSIONSSERVICESTUB_HPP_

#include <memory>
#include <mutex>
#include <string>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>
#include <iamanager/v6/iamanager.pb.h>

/**
 * Test stub for IAMPermissionsService v6.
 */
class IAMPermissionsServiceStub final : public iamanager::v6::IAMPermissionsService::Service {
public:
    IAMPermissionsServiceStub()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8011", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        mServer = builder.BuildAndStart();
    }

    ~IAMPermissionsServiceStub()
    {
        if (mServer) {
            mServer->Shutdown();
        }
    }

    void SetSecret(const std::string& secret)
    {
        std::lock_guard lock {mMutex};
        mSecret = secret;
    }

    std::string GetLastItemID() const
    {
        std::lock_guard lock {mMutex};
        return mLastItemID;
    }

    std::string GetLastSubjectID() const
    {
        std::lock_guard lock {mMutex};
        return mLastSubjectID;
    }

    uint64_t GetLastInstance() const
    {
        std::lock_guard lock {mMutex};
        return mLastInstance;
    }

    grpc::Status RegisterInstance([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::RegisterInstanceRequest*                   request,
        iamanager::v6::RegisterInstanceResponse*                        response) override
    {
        std::lock_guard lock {mMutex};

        mLastItemID    = request->instance().item_id();
        mLastSubjectID = request->instance().subject_id();
        mLastInstance  = request->instance().instance();

        response->set_secret(mSecret);

        return grpc::Status::OK;
    }

    grpc::Status UnregisterInstance([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::UnregisterInstanceRequest*                   request,
        [[maybe_unused]] google::protobuf::Empty*                         response) override
    {
        std::lock_guard lock {mMutex};

        mLastItemID    = request->instance().item_id();
        mLastSubjectID = request->instance().subject_id();
        mLastInstance  = request->instance().instance();

        return grpc::Status::OK;
    }

private:
    std::unique_ptr<grpc::Server> mServer;
    mutable std::mutex            mMutex;
    std::string                   mSecret;
    std::string                   mLastItemID;
    std::string                   mLastSubjectID;
    uint64_t                      mLastInstance {0};
};

#endif
