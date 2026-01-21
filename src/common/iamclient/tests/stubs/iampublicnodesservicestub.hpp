/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICNODESSERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICNODESSERVICESTUB_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>
#include <iamanager/v6/iamanager.pb.h>

/**
 * Test stub for IAMPublicNodesService v6.
 */
class IAMPublicNodesServiceStub final : public iamanager::v6::IAMPublicNodesService::Service {
public:
    IAMPublicNodesServiceStub()
    {
        grpc::ServerBuilder builder;

        builder.AddListeningPort("localhost:8007", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        mServer = builder.BuildAndStart();
    }

    ~IAMPublicNodesServiceStub()
    {
        if (mServer) {
            mServer->Shutdown();
        }
    }

    void SetNodeIds(const std::vector<std::string>& nodeIds)
    {
        std::lock_guard lock {mMutex};

        mNodeIds = nodeIds;
    }

    void SetNodeInfo(const std::string& nodeID, const std::string& nodeType)
    {
        std::lock_guard lock {mMutex};

        mNodeInfos[nodeID] = nodeType;
    }

    bool SendNodeInfoChanged(const std::string& nodeID, const std::string& nodeType)
    {
        std::lock_guard lock {mMutex};

        if (!mWriter) {
            return false;
        }

        iamanager::v6::NodeInfo nodeInfo;

        nodeInfo.set_node_id(nodeID);
        nodeInfo.set_node_type(nodeType);
        nodeInfo.set_state("provisioned");

        return mWriter->Write(nodeInfo);
    }

    bool WaitForConnection(std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};

        return mCV.wait_for(lock, timeout, [this] { return mWriter != nullptr; });
    }

    bool WaitForRegisterNodeConnection(std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mRegisterNodeMutex};

        return mRegisterNodeCV.wait_for(lock, timeout, [this] { return mRegisterNodeStream != nullptr; });
    }

    bool SendIncomingMessage(const iamanager::v6::IAMIncomingMessages& message)
    {
        std::lock_guard lock {mRegisterNodeMutex};

        if (!mRegisterNodeStream) {
            return false;
        }

        return mRegisterNodeStream->Write(message);
    }

    bool WaitForOutgoingMessage(
        iamanager::v6::IAMOutgoingMessages& message, std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mRegisterNodeMutex};

        if (!mRegisterNodeCV.wait_for(lock, timeout, [this] { return !mReceivedMessages.empty(); })) {
            return false;
        }

        message = mReceivedMessages.front();
        mReceivedMessages.pop();

        return true;
    }

    size_t GetReceivedMessagesCount()
    {
        std::lock_guard lock {mRegisterNodeMutex};

        return mReceivedMessages.size();
    }

    grpc::Status GetAllNodeIDs([[maybe_unused]] grpc::ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty* request, iamanager::v6::NodesID* response) override
    {
        std::lock_guard lock {mMutex};

        for (const auto& nodeId : mNodeIds) {
            response->add_ids(nodeId);
        }

        return grpc::Status::OK;
    }

    grpc::Status GetNodeInfo([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::GetNodeInfoRequest* request, iamanager::v6::NodeInfo* response) override
    {
        std::lock_guard lock {mMutex};

        auto it = mNodeInfos.find(request->node_id());
        if (it == mNodeInfos.end()) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");
        }

        response->set_node_id(request->node_id());
        response->set_node_type(it->second);
        response->set_state("provisioned");

        return grpc::Status::OK;
    }

    grpc::Status SubscribeNodeChanged(grpc::ServerContext* context,
        [[maybe_unused]] const google::protobuf::Empty*    request,
        grpc::ServerWriter<iamanager::v6::NodeInfo>*       writer) override
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

    grpc::Status RegisterNode([[maybe_unused]] grpc::ServerContext*                                       context,
        grpc::ServerReaderWriter<iamanager::v6::IAMIncomingMessages, iamanager::v6::IAMOutgoingMessages>* stream)
        override
    {
        {
            std::lock_guard lock {mRegisterNodeMutex};

            mRegisterNodeStream = stream;
            mRegisterNodeCV.notify_all();
        }

        iamanager::v6::IAMOutgoingMessages outgoingMsg;

        while (stream->Read(&outgoingMsg)) {
            std::lock_guard lock {mRegisterNodeMutex};

            mReceivedMessages.push(outgoingMsg);
            mRegisterNodeCV.notify_all();
        }

        {
            std::lock_guard lock {mRegisterNodeMutex};

            mRegisterNodeStream = nullptr;
        }

        return grpc::Status::OK;
    }

private:
    std::unique_ptr<grpc::Server>                mServer;
    mutable std::mutex                           mMutex;
    std::condition_variable                      mCV;
    grpc::ServerWriter<iamanager::v6::NodeInfo>* mWriter {nullptr};
    std::vector<std::string>                     mNodeIds;
    std::map<std::string, std::string>           mNodeInfos;

    // RegisterNode support
    mutable std::mutex      mRegisterNodeMutex;
    std::condition_variable mRegisterNodeCV;
    grpc::ServerReaderWriter<iamanager::v6::IAMIncomingMessages, iamanager::v6::IAMOutgoingMessages>*
                                                   mRegisterNodeStream {nullptr};
    std::queue<iamanager::v6::IAMOutgoingMessages> mReceivedMessages;
};

#endif
