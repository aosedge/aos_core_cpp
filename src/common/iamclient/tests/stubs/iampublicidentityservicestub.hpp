/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICIDENTITYSERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICIDENTITYSERVICESTUB_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>
#include <iamanager/v6/iamanager.pb.h>

/**
 * Test stub for IAMPublicIdentityService v6.
 */
class IAMPublicIdentityServiceStub final : public iamanager::v6::IAMPublicIdentityService::Service {
public:
    IAMPublicIdentityServiceStub()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8006", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        mServer = builder.BuildAndStart();
    }

    ~IAMPublicIdentityServiceStub()
    {
        if (mServer) {
            mServer->Shutdown();
        }
    }

    void SetSystemInfo(const std::string& systemID, const std::string& unitModel)
    {
        std::lock_guard lock {mMutex};
        mSystemID  = systemID;
        mUnitModel = unitModel;
    }

    void SetSubjects(const std::vector<std::string>& subjects)
    {
        std::lock_guard lock {mMutex};
        mSubjects = subjects;
    }

    bool SendSubjectsChanged(const std::vector<std::string>& subjects)
    {
        std::lock_guard lock {mMutex};
        if (!mWriter) {
            return false;
        }

        iamanager::v6::Subjects subjectsMsg;
        for (const auto& subject : subjects) {
            subjectsMsg.add_subjects(subject);
        }

        return mWriter->Write(subjectsMsg);
    }

    bool WaitForConnection(std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};
        return mCV.wait_for(lock, timeout, [this] { return mWriter != nullptr; });
    }

    grpc::Status GetSystemInfo([[maybe_unused]] grpc::ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty* request, iamanager::v6::SystemInfo* response) override
    {
        std::lock_guard lock {mMutex};

        response->set_system_id(mSystemID);
        response->set_unit_model(mUnitModel);

        return grpc::Status::OK;
    }

    grpc::Status GetSubjects([[maybe_unused]] grpc::ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty* request, iamanager::v6::Subjects* response) override
    {
        std::lock_guard lock {mMutex};

        for (const auto& subject : mSubjects) {
            response->add_subjects(subject);
        }

        return grpc::Status::OK;
    }

    grpc::Status SubscribeSubjectsChanged(grpc::ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty*        request,
        grpc::ServerWriter<iamanager::v6::Subjects>*           writer) override
    {
        {
            std::lock_guard lock {mMutex};
            mWriter = writer;
            mCV.notify_all();
        }

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        {
            std::lock_guard lock {mMutex};
            if (mWriter == writer) {
                mWriter = nullptr;
            }
        }

        return grpc::Status::OK;
    }

private:
    std::unique_ptr<grpc::Server>                mServer;
    mutable std::mutex                           mMutex;
    std::condition_variable                      mCV;
    grpc::ServerWriter<iamanager::v6::Subjects>* mWriter {nullptr};
    std::string                                  mSystemID;
    std::string                                  mUnitModel;
    std::vector<std::string>                     mSubjects;
};

#endif
