/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PUBLICMESSAGEHANDLER_HPP_
#define PUBLICMESSAGEHANDLER_HPP_

#include <array>
#include <optional>
#include <shared_mutex>
#include <string>

#include <grpcpp/server_builder.h>

#include <aos/common/cryptoutils.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/identhandler.hpp>
#include <aos/iam/nodeinfoprovider.hpp>
#include <aos/iam/nodemanager.hpp>
#include <aos/iam/permhandler.hpp>
#include <aos/iam/provisionmanager.hpp>

#include <iamanager/version.grpc.pb.h>

#include "nodecontroller.hpp"

/**
 * Public message handler. Responsible for handling public IAM services.
 */
class PublicMessageHandler :
    // public services
    protected iamanager::IAMVersionService::Service,
    protected iamproto::IAMPublicService::Service,
    protected iamproto::IAMPublicIdentityService::Service,
    protected iamproto::IAMPublicPermissionsService::Service,
    protected iamproto::IAMPublicNodesService::Service,
    // NodeInfo listener interface.
    public aos::iam::nodemanager::NodeInfoListenerItf,
    // identhandler subject observer interface
    public aos::iam::identhandler::SubjectsObserverItf {
public:
    /**
     * Initializes public message handler instance.
     *
     * @param nodeController node controller.
     * @param identHandler identification handler.
     * @param permHandler permission handler.
     * @param nodeInfoProvider node info provider.
     * @param nodeManager node manager.
     * @param provisionManager provision manager.
     */
    aos::Error Init(NodeController& nodeController, aos::iam::identhandler::IdentHandlerItf& identHandler,
        aos::iam::permhandler::PermHandlerItf& permHandler, aos::iam::NodeInfoProviderItf& nodeInfoProvider,
        aos::iam::nodemanager::NodeManagerItf&           nodeManager,
        aos::iam::provisionmanager::ProvisionManagerItf& provisionManager);

    /**
     * Registers grpc services.
     *
     * @param builder server builder.
     */
    void RegisterServices(grpc::ServerBuilder& builder);

    /**
     * Node info change notification.
     *
     * @param info node info.
     */
    void OnNodeInfoChange(const aos::NodeInfo& info) override;

    /**
     * Node info removed notification.
     *
     * @param id id of the node been removed.
     */
    void OnNodeRemoved(const aos::String& id) override;

    /**
     * Subjects observer interface implementation.
     *
     * @param[in] messages subject changed messages.
     * @returns Error.
     */
    aos::Error SubjectsChanged(const aos::Array<aos::StaticString<aos::cSubjectIDLen>>& messages) override;

    /**
     * Closes public message handler.
     */
    void Close();

protected:
    /**
     * Server writer controller handles server writer streams.
     */
    template <typename T>
    class ServerWriterController {
    public:
        /**
         * Closes all streams.
         */
        void Close()
        {
            mIsRunning = false;

            {
                std::unique_lock lock {mMutex};

                mLastMessage.reset();
            }

            mCV.notify_all();
        }

        /**
         * Writes notification message to all streams.
         *
         * @param message notification message.
         */
        void WriteToStreams(const T& message)
        {
            {
                std::unique_lock lock {mMutex};

                ++mNotificationID;

                mLastMessage = message;
            }

            mCV.notify_all();
        }

        /**
         * Handles stream. Blocks the caller until the stream is closed.
         *
         * @param context server context.
         * @param writer server writer.
         * @return grpc::Status.
         */
        grpc::Status HandleStream(grpc::ServerContext* context, grpc::ServerWriter<T>* writer)
        {
            uint32_t lastNotificationID = 0;

            while (mIsRunning && !context->IsCancelled()) {
                std::shared_lock lock {mMutex};

                if (mCV.wait_for(lock, cWaitTimeout, [this, lastNotificationID] {
                        return mNotificationID != lastNotificationID && mLastMessage.has_value();
                    })) {
                    // got notification, send it to the client
                    if (!writer->Write(*mLastMessage)) {
                        break;
                    }

                    lastNotificationID = mNotificationID;
                }
            }

            return grpc::Status::OK;
        }

    private:
        static constexpr auto cWaitTimeout = std::chrono::seconds(10);

        std::atomic_bool            mIsRunning = true;
        std::condition_variable_any mCV;
        std::shared_mutex           mMutex;
        std::atomic_uint32_t        mNotificationID = 0;
        std::optional<T>            mLastMessage;
    };

    aos::iam::identhandler::IdentHandlerItf*         GetIdentHandler() { return mIdentHandler; }
    aos::iam::permhandler::PermHandlerItf*           GetPermHandler() { return mPermHandler; }
    aos::iam::NodeInfoProviderItf*                   GetNodeInfoProvider() { return mNodeInfoProvider; }
    NodeController*                                  GetNodeController() { return mNodeController; }
    aos::NodeInfo&                                   GetNodeInfo() { return mNodeInfo; }
    aos::iam::nodemanager::NodeManagerItf*           GetNodeManager() { return mNodeManager; }
    aos::iam::provisionmanager::ProvisionManagerItf* GetProvisionManager() { return mProvisionManager; }
    bool                                             IsMainNode() const;
    aos::Error                                       SetNodeStatus(const aos::NodeStatus& status);

private:
    // IAMVersionService interface
    grpc::Status GetAPIVersion(
        grpc::ServerContext* context, const google::protobuf::Empty* request, iamanager::APIVersion* response) override;

    // IAMPublicService interface
    grpc::Status GetNodeInfo(
        grpc::ServerContext* context, const google::protobuf::Empty* request, iamproto::NodeInfo* response) override;
    grpc::Status GetCert(grpc::ServerContext* context, const iamproto::GetCertRequest* request,
        iamproto::GetCertResponse* response) override;

    // IAMPublicIdentityService interface
    grpc::Status GetSystemInfo(
        grpc::ServerContext* context, const google::protobuf::Empty* request, iamproto::SystemInfo* response) override;
    grpc::Status GetSubjects(
        grpc::ServerContext* context, const google::protobuf::Empty* request, iamproto::Subjects* response) override;
    grpc::Status SubscribeSubjectsChanged(grpc::ServerContext* context, const google::protobuf::Empty* request,
        grpc::ServerWriter<iamproto::Subjects>* writer) override;

    // IAMPublicPermissionsService interface
    grpc::Status GetPermissions(grpc::ServerContext* context, const iamproto::PermissionsRequest* request,
        iamproto::PermissionsResponse* response) override;

    // IAMPublicNodesService interface
    grpc::Status GetAllNodeIDs(
        grpc::ServerContext* context, const google::protobuf::Empty* request, iamproto::NodesID* response) override;
    grpc::Status GetNodeInfo(grpc::ServerContext* context, const iamproto::GetNodeInfoRequest* request,
        iamproto::NodeInfo* response) override;
    grpc::Status SubscribeNodeChanged(grpc::ServerContext* context, const google::protobuf::Empty* request,
        grpc::ServerWriter<iamproto::NodeInfo>* writer) override;
    grpc::Status RegisterNode(grpc::ServerContext*                                                  context,
        grpc::ServerReaderWriter<::iamproto::IAMIncomingMessages, ::iamproto::IAMOutgoingMessages>* stream) override;

    static constexpr auto       cIamAPIVersion            = 5;
    static constexpr auto       cNodeTypeTag              = "NODE_TYPE";
    static constexpr auto       cNodeTypeTagMainNodeValue = "main";
    static constexpr std::array cAllowedStatuses          = {aos::NodeStatusEnum::eUnprovisioned};

    aos::iam::identhandler::IdentHandlerItf*         mIdentHandler     = nullptr;
    aos::iam::permhandler::PermHandlerItf*           mPermHandler      = nullptr;
    aos::iam::NodeInfoProviderItf*                   mNodeInfoProvider = nullptr;
    aos::iam::nodemanager::NodeManagerItf*           mNodeManager      = nullptr;
    aos::iam::provisionmanager::ProvisionManagerItf* mProvisionManager = nullptr;
    NodeController*                                  mNodeController   = nullptr;
    ServerWriterController<iamproto::NodeInfo>       mNodeChangedController;
    ServerWriterController<iamproto::Subjects>       mSubjectsChangedController;
    aos::NodeInfo                                    mNodeInfo;
};

#endif
