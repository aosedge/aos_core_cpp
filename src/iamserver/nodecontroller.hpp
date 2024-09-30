/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NODECONTROLLER_HPP_
#define NODECONTROLLER_HPP_

#include <future>
#include <string>

#include <Poco/Event.h>

#include <aos/common/types.hpp>
#include <aos/iam/nodemanager.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

namespace iamproto = iamanager::v5;

using NodeServerReaderWriter = grpc::ServerReaderWriter<iamproto::IAMIncomingMessages, iamproto::IAMOutgoingMessages>;

using PendingMessagesMap
    = std::map<iamproto::IAMOutgoingMessages::IAMOutgoingMessageCase, std::promise<iamproto::IAMOutgoingMessages>>;

/**
 * Handles register node input/output stream.
 */
class NodeStreamHandler {
public:
    /**
     * Constructor.
     *
     * @param allowedStatuses allowed node statuses.
     * @param stream rpc stream to handle.
     * @param context server context.
     * @param nodeManager node manager.
     */
    NodeStreamHandler(const std::vector<aos::NodeStatus>& allowedStatuses, NodeServerReaderWriter* stream,
        grpc::ServerContext* context, aos::iam::nodemanager::NodeManagerItf* nodeManager);

    /**
     * Destructor.
     */
    ~NodeStreamHandler();

    /**
     * Closes stream handler.
     */
    void Close();

    /**
     * Gets node id that current handler handles.
     *
     * @return std::string.
     */
    std::string GetNodeID() const;

    /**
     * Handles input/output streams. This method is blocking and should be called in a separate thread.
     *
     * @return aos::Error.
     */
    aos::Error HandleStream();

    /**
     * Sends get cert types request and waits for response with timeout.
     *
     * @param request get cert types request.
     * @param response[out] cert types response.
     * @param responseTimeout response timeout.
     * @return grpc::Status.
     */
    grpc::Status GetCertTypes(const iamproto::GetCertTypesRequest* request, iamproto::CertTypes* response,
        const std::chrono::seconds responseTimeout);

    /**
     * Sends start provisioning request and waits for response with timeout.
     *
     * @param request start provisioning request.
     * @param[out] response start provisioning response.
     * @param responseTimeout response timeout.
     * @return grpc::Status.
     */
    grpc::Status StartProvisioning(const iamproto::StartProvisioningRequest* request,
        iamproto::StartProvisioningResponse* response, const std::chrono::seconds responseTimeout);

    /**
     * Sends finish provisioning request and waits for response with timeout.
     *
     * @param request finish provisioning request.
     * @param[out] response finish provisioning response.
     * @param responseTimeout response timeout.
     * @return grpc::Status.
     */
    grpc::Status FinishProvisioning(const iamproto::FinishProvisioningRequest* request,
        iamproto::FinishProvisioningResponse* response, const std::chrono::seconds responseTimeout);

    /**
     * Sends deprovision request and waits for response with timeout.
     *
     * @param request deprovision request.
     * @param[out] response deprovision response.
     * @param responseTimeout response timeout.
     * @return grpc::Status.
     */
    grpc::Status Deprovision(const iamproto::DeprovisionRequest* request, iamproto::DeprovisionResponse* response,
        const std::chrono::seconds responseTimeout);

    /**
     * Sends pause node request and waits for response with timeout.
     *
     * @param request pause node request.
     * @param[out] response pause node response.
     * @param responseTimeout response timeout.
     * @return grpc::Status.
     */
    grpc::Status PauseNode(const iamproto::PauseNodeRequest* request, iamproto::PauseNodeResponse* response,
        const std::chrono::seconds responseTimeout);

    /**
     * Sends resume node request and waits for response with timeout.
     *
     * @param request resume node request.
     * @param[out] response resume node response.
     * @param responseTimeout response timeout.
     * @return grpc::Status.
     */
    grpc::Status ResumeNode(const iamproto::ResumeNodeRequest* request, iamproto::ResumeNodeResponse* response,
        const std::chrono::seconds responseTimeout);

    /**
     * Sends create key request and waits for response with timeout.
     *
     * @param request create key request.
     * @param[out] response create key response.
     * @param responseTimeout response timeout.
     * @return grpc::Status.
     */
    grpc::Status CreateKey(const iamproto::CreateKeyRequest* request, iamproto::CreateKeyResponse* response,
        const std::chrono::seconds responseTimeout);

    /**
     * Sends apply cert request and waits for response with timeout.
     *
     * @param request apply certificate request.
     * @param[out] response apply certificate response.
     * @param responseTimeout response timeout.
     * @return grpc::Status.
     */
    grpc::Status ApplyCert(const iamproto::ApplyCertRequest* request, iamproto::ApplyCertResponse* response,
        const std::chrono::seconds responseTimeout);

private:
    aos::Error SendMessage(const iamproto::IAMIncomingMessages& request, iamproto::IAMOutgoingMessages& response,
        const std::chrono::seconds responseTimeout);
    aos::Error HandleNodeInfo(const iamproto::NodeInfo& info);
    void       SetNodeID(const std::string& nodeID);

    mutable std::mutex mNodeIDMutex;
    std::string        mNodeID;

    std::vector<aos::NodeStatus>           mAllowedStatuses;
    NodeServerReaderWriter*                mStream      = nullptr;
    grpc::ServerContext*                   mContext     = nullptr;
    aos::iam::nodemanager::NodeManagerItf* mNodeManager = nullptr;
    std::mutex                             mMutex;
    std::atomic_bool                       mIsClosed = false;
    PendingMessagesMap                     mPendingMessages;
};

using NodeStreamHandlerPtr = std::shared_ptr<NodeStreamHandler>;

/**
 * Register node manager manages register node stream handlers.
 */
class NodeController {
public:
    /**
     * Constructor.
     */
    NodeController();

    /**
     * Starts node controller.
     */
    void Start();

    /**
     * Closes all stream handlers.
     */
    void Close();

    /**
     * Handles register node input/output streams.
     * This method is blocking and should be called in a separate thread.
     *
     * @param allowedStatuses allowed node statuses.
     * @param stream rpc stream to handle.
     * @param context server context.
     * @param nodeManager node manager.
     * @return grpc::Status.
     */
    grpc::Status HandleRegisterNodeStream(const std::vector<aos::NodeStatus>& allowedStatuses,
        NodeServerReaderWriter* stream, grpc::ServerContext* context,
        aos::iam::nodemanager::NodeManagerItf* nodeManager);

    /**
     * Gets node stream handler by node id.
     *
     * @param nodeID node id.
     * @return NodeStreamHandlerPtr.
     */
    NodeStreamHandlerPtr GetNodeStreamHandler(const std::string& nodeID);

private:
    void StoreHandler(NodeStreamHandlerPtr handler);
    void RemoveHandler(NodeStreamHandlerPtr handler);

    bool                              mIsClosed = false;
    std::mutex                        mMutex;
    std::vector<NodeStreamHandlerPtr> mHandlers;
};

#endif
