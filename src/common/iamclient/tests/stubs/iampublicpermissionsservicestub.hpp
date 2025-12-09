/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICPERMISSIONSSERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICPERMISSIONSSERVICESTUB_HPP_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>
#include <iamanager/v6/iamanager.pb.h>

/**
 * Test stub for IAMPublicPermissionsService v6.
 */
class IAMPublicPermissionsServiceStub final : public iamanager::v6::IAMPublicPermissionsService::Service {
public:
    IAMPublicPermissionsServiceStub()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8012", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        mServer = builder.BuildAndStart();
    }

    ~IAMPublicPermissionsServiceStub()
    {
        if (mServer) {
            mServer->Shutdown();
        }
    }

    void SetInstanceIdent(const std::string& itemID, const std::string& subjectID, uint64_t instance)
    {
        std::lock_guard lock {mMutex};

        mItemID    = itemID;
        mSubjectID = subjectID;
        mInstance  = instance;
    }

    void SetPermissions(const std::vector<std::string>& funcIDs)
    {
        std::lock_guard lock {mMutex};

        mFuncIDs = funcIDs;
    }

    std::string GetLastSecret() const
    {
        std::lock_guard lock {mMutex};

        return mLastSecret;
    }

    std::string GetLastFuncServerID() const
    {
        std::lock_guard lock {mMutex};

        return mLastFuncServerID;
    }

    grpc::Status GetPermissions([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::PermissionsRequest* request, iamanager::v6::PermissionsResponse* response) override
    {
        std::lock_guard lock {mMutex};

        mLastSecret       = request->secret();
        mLastFuncServerID = request->functional_server_id();

        response->mutable_instance()->set_item_id(mItemID);
        response->mutable_instance()->set_subject_id(mSubjectID);
        response->mutable_instance()->set_instance(mInstance);

        auto* permissionsMap = response->mutable_permissions()->mutable_permissions();
        for (const auto& funcID : mFuncIDs) {
            (*permissionsMap)[funcID] = "";
        }

        return grpc::Status::OK;
    }

private:
    std::unique_ptr<grpc::Server> mServer;
    mutable std::mutex            mMutex;
    std::string                   mItemID;
    std::string                   mSubjectID;
    uint64_t                      mInstance {0};
    std::vector<std::string>      mFuncIDs;
    std::string                   mLastSecret;
    std::string                   mLastFuncServerID;
};

#endif
