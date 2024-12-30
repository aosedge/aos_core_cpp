/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NODECONTROLLER_HPP_
#define NODECONTROLLER_HPP_

#include <future>
#include <map>
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
class NodeStreamHandler : public std::enable_shared_from_this<NodeStreamHandler> {
public:
    using Ptr = std::shared_ptr<NodeStreamHandler>;

    /**
     * Stream registry interface.
     */
    struct StreamRegistryItf {
        /**
         * Links nodeID to handler. If nodeID is already linked to another handler, it will be reset.
         *
         * @param nodeID node identifier.
         * @param handler node stream handler.
         */
        virtual void LinkNodeIDToHandler(const std::string& nodeID, NodeStreamHandler::Ptr handler) = 0;

        /**
         * Unlinks nodeID from handler.
         *
         * @param handler node stream handler.
         */
        virtual void UnlinkNodeIDFromHandler(NodeStreamHandler::Ptr handler) = 0;

        /**
         * Destroys object instance.
         */
        virtual ~StreamRegistryItf() = default;
    };

    /**
     * Creates instance.
     *
     * @param allowedStatuses allowed node statuses.
     * @param stream rpc stream to handle.
     * @param context server context.
     * @param nodeManager node manager.
     * @param streamRegistry stream registry.
     */
    static NodeStreamHandler::Ptr Create(const std::vector<aos::NodeStatus>& allowedStatuses,
        NodeServerReaderWriter* stream, grpc::ServerContext* context,
        aos::iam::nodemanager::NodeManagerItf* nodeManager, StreamRegistryItf* streamRegistry);

    /**
     * Destructor.
     */
    ~NodeStreamHandler();

    /**
     * Closes stream handler.
     */
    void Close();

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
    NodeStreamHandler(const std::vector<aos::NodeStatus>& allowedStatuses, NodeServerReaderWriter* stream,
        grpc::ServerContext* context, aos::iam::nodemanager::NodeManagerItf* nodeManager,
        StreamRegistryItf* streamRegistry);

    aos::Error SendMessage(const iamproto::IAMIncomingMessages& request, iamproto::IAMOutgoingMessages& response,
        const std::chrono::seconds responseTimeout);
    aos::Error HandleNodeInfo(const iamproto::NodeInfo& info);

    std::vector<aos::NodeStatus>           mAllowedStatuses;
    NodeServerReaderWriter*                mStream         = nullptr;
    grpc::ServerContext*                   mContext        = nullptr;
    aos::iam::nodemanager::NodeManagerItf* mNodeManager    = nullptr;
    StreamRegistryItf*                     mStreamRegistry = nullptr;
    std::mutex                             mMutex;
    std::atomic_bool                       mIsClosed = false;
    PendingMessagesMap                     mPendingMessages;
};

/**
 * Node controller manages register node stream handlers.
 */
class NodeController : private NodeStreamHandler::StreamRegistryItf {
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
     * @return NodeStreamHandler::Ptr.
     */
    NodeStreamHandler::Ptr GetNodeStreamHandler(const std::string& nodeID);

private:
    void LinkNodeIDToHandler(const std::string& nodeID, NodeStreamHandler::Ptr handler) override;
    void UnlinkNodeIDFromHandler(NodeStreamHandler::Ptr handler) override;
    void Store(NodeStreamHandler::Ptr handler);
    void Remove(NodeStreamHandler::Ptr handler);

    bool                                          mIsClosed = false;
    std::mutex                                    mMutex;
    std::map<NodeStreamHandler::Ptr, std::string> mHandlers;
};

#endif
