/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICCURRENTNODESERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICCURRENTNODESERVICESTUB_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>
#include <iamanager/v6/iamanager.pb.h>

/**
 * Test stub for IAMPublicCurrentNodeService v6.
 */
class IAMPublicCurrentNodeServiceStub final : public iamanager::v6::IAMPublicCurrentNodeService::Service {
public:
    IAMPublicCurrentNodeServiceStub()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8005", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        mServer = builder.BuildAndStart();
    }

    ~IAMPublicCurrentNodeServiceStub()
    {
        if (mServer) {
            mServer->Shutdown();
        }
    }

    void SetNodeInfo(const std::string& nodeID, const std::string& nodeType)
    {
        std::lock_guard lock {mMutex};
        mNodeID   = nodeID;
        mNodeType = nodeType;
    }

    bool SendNodeInfoChanged(const std::string& nodeID, const std::string& nodeType)
    {
        std::unique_lock lock {mMutex};
        if (!mWriter) {
            return false;
        }

        iamanager::v6::NodeInfo nodeInfo;
        nodeInfo.set_node_id(nodeID);
        nodeInfo.set_node_type(nodeType);

        return mWriter->Write(nodeInfo);
    }

    bool WaitForConnection(std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};
        return mCV.wait_for(lock, timeout, [this] { return mWriter != nullptr; });
    }

    std::string GetRequestedNodeID() const
    {
        std::lock_guard lock {mMutex};
        return mNodeID;
    }

    grpc::Status GetCurrentNodeInfo([[maybe_unused]] grpc::ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty* request, iamanager::v6::NodeInfo* response) override
    {
        std::lock_guard lock {mMutex};

        response->set_node_id(mNodeID);
        response->set_node_type(mNodeType);

        return grpc::Status::OK;
    }

    grpc::Status SubscribeCurrentNodeChanged(grpc::ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty*           request,
        grpc::ServerWriter<iamanager::v6::NodeInfo>*              writer) override
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
    grpc::ServerWriter<iamanager::v6::NodeInfo>* mWriter {nullptr};
    std::string                                  mNodeID;
    std::string                                  mNodeType;
};

#endif
