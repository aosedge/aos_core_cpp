/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_IAMSERVER_PUBLICMESSAGEHANDLER_HPP_
#define AOS_IAM_IAMSERVER_PUBLICMESSAGEHANDLER_HPP_

#include <array>
#include <optional>
#include <shared_mutex>
#include <string>

#include <grpcpp/server_builder.h>

#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/iamclient/itf/identprovider.hpp>
#include <core/common/iamclient/itf/nodeinfoprovider.hpp>
#include <core/iam/certhandler/certhandler.hpp>
#include <core/iam/nodeinfoprovider/itf/nodeinfoprovider.hpp>
#include <core/iam/nodemanager/itf/nodemanager.hpp>
#include <core/iam/permhandler/itf/permhandler.hpp>

#include <common/pbconvert/common.hpp>
#include <common/pbconvert/iam.hpp>
#include <iamanager/version.grpc.pb.h>

#include "nodecontroller.hpp"
#include "streamwriter.hpp"

namespace aos::iam::iamserver {

/**
 * Public message handler. Responsible for handling public IAM services.
 */
class PublicMessageHandler :
    // public services
    protected iamanager::IAMVersionService::Service,
    protected iamproto::IAMPublicCurrentNodeService::Service,
    protected iamproto::IAMPublicCertService::Service,
    protected iamproto::IAMPublicIdentityService::Service,
    protected iamproto::IAMPublicPermissionsService::Service,
    protected iamproto::IAMPublicNodesService::Service,
    // NodeInfo listener interface.
    public iam::nodemanager::NodeInfoListenerItf,
    // identhandler subject observer interface
    public aos::iamclient::SubjectsListenerItf {
public:
    /**
     * Initializes public message handler instance.
     *
     * @param nodeController node controller.
     * @param identProvider identification provider.
     * @param permHandler permission handler.
     * @param nodeInfoProvider node info provider.
     * @param nodeManager node manager.
     * @param certProvider certificate provider.
     */
    Error Init(NodeController& nodeController, aos::iamclient::IdentProviderItf& identProvider,
        iam::permhandler::PermHandlerItf& permHandler, iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
        iam::nodemanager::NodeManagerItf& nodeManager, aos::iamclient::CertProviderItf& certProvider);

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
    void OnNodeInfoChange(const NodeInfo& info) override;

    /**
     * Node info removed notification.
     *
     * @param id id of the node been removed.
     */
    void OnNodeRemoved(const String& id) override;

    /**
     * Notifies about subjects change.
     *
     * @param subjects subject changed messages.
     * @returns Error.
     */
    void SubjectsChanged(const Array<StaticString<cIDLen>>& subjects) override;

    /**
     * Start public message handler.
     */
    void Start();

    /**
     * Closes public message handler.
     */
    void Close();

protected:
    aos::iamclient::IdentProviderItf*           GetIdentProvider() { return mIdentProvider; }
    iam::permhandler::PermHandlerItf*           GetPermHandler() { return mPermHandler; }
    iam::nodeinfoprovider::NodeInfoProviderItf* GetNodeInfoProvider() { return mNodeInfoProvider; }
    NodeController*                             GetNodeController() { return mNodeController; }
    NodeInfo&                                   GetNodeInfo() { return mNodeInfo; }
    iam::nodemanager::NodeManagerItf*           GetNodeManager() { return mNodeManager; }
    Error SetNodeState(const std::string& nodeID, const NodeState& state, bool provisioned);
    bool  ProcessOnThisNode(const std::string& nodeID);

    template <typename R>
    grpc::Status RequestWithRetry(R request)
    {
        std::unique_lock lock {mMutex};

        grpc::Status status = grpc::Status::OK;

        for (auto i = 0; i < cRequestRetryMaxTry; i++) {
            if (mClose) {
                return common::pbconvert::ConvertAosErrorToGrpcStatus({ErrorEnum::eWrongState, "handler is closed"});
            }

            if (status = request(); status.ok()) {
                return status;
            }

            mRetryCondVar.wait_for(lock, cRequestRetryTimeout, [this] { return mClose; });
        }

        return status;
    }

private:
    static constexpr auto cIamAPIVersion       = 6;
    static constexpr auto cProvisioned         = false;
    static constexpr auto cRequestRetryTimeout = std::chrono::seconds(10);
    static constexpr auto cRequestRetryMaxTry  = 3;

    // IAMVersionService interface
    grpc::Status GetAPIVersion(
        grpc::ServerContext* context, const google::protobuf::Empty* request, iamanager::APIVersion* response) override;

    // IAMPublicCurrentNodeService interface
    ::grpc::Status GetCurrentNodeInfo(::grpc::ServerContext* context, const ::google::protobuf::Empty* request,
        ::iamanager::v6::NodeInfo* response) override;
    ::grpc::Status SubscribeCurrentNodeChanged(::grpc::ServerContext* context, const ::google::protobuf::Empty* request,
        ::grpc::ServerWriter<::iamanager::v6::NodeInfo>* writer) override;

    // IAMPublicCertService interface
    ::grpc::Status GetCert(::grpc::ServerContext* context, const ::iamanager::v6::GetCertRequest* request,
        ::iamanager::v6::CertInfo* response) override;
    ::grpc::Status SubscribeCertChanged(::grpc::ServerContext* context,
        const ::iamanager::v6::SubscribeCertChangedRequest*    request,
        ::grpc::ServerWriter<::iamanager::v6::CertInfo>*       writer) override;

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
    grpc::Status RegisterNode(grpc::ServerContext*                                              context,
        grpc::ServerReaderWriter<iamproto::IAMIncomingMessages, iamproto::IAMOutgoingMessages>* stream) override;

    aos::iamclient::IdentProviderItf*           mIdentProvider    = nullptr;
    iam::permhandler::PermHandlerItf*           mPermHandler      = nullptr;
    iam::nodeinfoprovider::NodeInfoProviderItf* mNodeInfoProvider = nullptr;
    iam::nodemanager::NodeManagerItf*           mNodeManager      = nullptr;
    aos::iamclient::CertProviderItf*            mCertProvider     = nullptr;
    NodeController*                             mNodeController   = nullptr;
    StreamWriter<iamproto::NodeInfo>            mCurrentNodeChangedController;
    StreamWriter<iamproto::NodeInfo>            mNodeChangedController;
    StreamWriter<iamproto::Subjects>            mSubjectsChangedController;
    NodeInfo                                    mNodeInfo;

    std::vector<std::shared_ptr<CertWriter>> mCertWriters;
    std::mutex                               mCertWritersLock;
    std::condition_variable                  mRetryCondVar;
    std::mutex                               mMutex;
    bool                                     mClose = false;
};

} // namespace aos::iam::iamserver

#endif
