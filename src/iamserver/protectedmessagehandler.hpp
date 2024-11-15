/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PROTECTEDMESSAGEHANDLER_HPP_
#define PROTECTEDMESSAGEHANDLER_HPP_

#include <array>
#include <chrono>
#include <string>

#include <grpcpp/server_builder.h>

#include <aos/common/cryptoutils.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/identhandler.hpp>
#include <aos/iam/nodeinfoprovider.hpp>
#include <aos/iam/permhandler.hpp>
#include <config/config.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include "nodecontroller.hpp"
#include "publicmessagehandler.hpp"

/**
 * Protected message handler. Responsible for handling protected IAM services.
 */
class ProtectedMessageHandler :
    // public services
    public PublicMessageHandler,
    // protected services
    private iamproto::IAMNodesService::Service,
    private iamproto::IAMProvisioningService::Service,
    private iamproto::IAMCertificateService::Service,
    private iamproto::IAMPermissionsService::Service {
public:
    /**
     * Initializes protected message handler instance.
     *
     * @param nodeController node controller.
     * @param identHandler identification handler.
     * @param permHandler permission handler.
     * @param nodeInfoProvider node info provider.
     * @param nodeManager node manager.
     * @param provisionManager provision manager.
     */
    aos::Error Init(NodeController& nodeController, aos::iam::identhandler::IdentHandlerItf& identHandler,
        aos::iam::permhandler::PermHandlerItf&           permHandler,
        aos::iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
        aos::iam::nodemanager::NodeManagerItf&           nodeManager,
        aos::iam::provisionmanager::ProvisionManagerItf& provisionManager);

    /**
     * Registers grpc services.
     *
     * @param builder server builder.
     */
    void RegisterServices(grpc::ServerBuilder& builder);

    using PublicMessageHandler::OnNodeInfoChange;
    using PublicMessageHandler::OnNodeRemoved;

    // identhandler::SubjectsObserverItf interface
    using PublicMessageHandler::SubjectsChanged;

    /**
     * Closes protected message handler.
     */
    void Close();

private:
    // IAMPublicNodesService interface
    grpc::Status RegisterNode(grpc::ServerContext*                                                  context,
        grpc::ServerReaderWriter<::iamproto::IAMIncomingMessages, ::iamproto::IAMOutgoingMessages>* stream) override;

    // IAMNodesService interface
    grpc::Status PauseNode(grpc::ServerContext* context, const iamproto::PauseNodeRequest* request,
        iamproto::PauseNodeResponse* response) override;
    grpc::Status ResumeNode(grpc::ServerContext* context, const iamproto::ResumeNodeRequest* request,
        iamproto::ResumeNodeResponse* response) override;

    // IAMProvisioningService interface
    grpc::Status GetCertTypes(grpc::ServerContext* context, const iamproto::GetCertTypesRequest* request,
        iamproto::CertTypes* response) override;
    grpc::Status StartProvisioning(grpc::ServerContext* context, const iamproto::StartProvisioningRequest* request,
        iamproto::StartProvisioningResponse* response) override;
    grpc::Status FinishProvisioning(grpc::ServerContext* context, const iamproto::FinishProvisioningRequest* request,
        iamproto::FinishProvisioningResponse* response) override;
    grpc::Status Deprovision(grpc::ServerContext* context, const iamproto::DeprovisionRequest* request,
        iamproto::DeprovisionResponse* response) override;

    // IAMCertificateService interface
    grpc::Status CreateKey(grpc::ServerContext* context, const iamproto::CreateKeyRequest* request,
        iamproto::CreateKeyResponse* response) override;
    grpc::Status ApplyCert(grpc::ServerContext* context, const iamproto::ApplyCertRequest* request,
        iamproto::ApplyCertResponse* response) override;

    // IAMPermissionsService interface
    grpc::Status RegisterInstance(grpc::ServerContext* context, const iamproto::RegisterInstanceRequest* request,
        iamproto::RegisterInstanceResponse* response) override;
    grpc::Status UnregisterInstance(grpc::ServerContext* context, const iamproto::UnregisterInstanceRequest* request,
        google::protobuf::Empty* response) override;

    bool ProcessOnThisNode(const std::string& nodeID);

    static constexpr auto       cDefaultTimeout      = std::chrono::minutes(1);
    static constexpr auto       cProvisioningTimeout = std::chrono::minutes(5);
    static constexpr std::array cAllowedStatuses = {aos::NodeStatusEnum::eProvisioned, aos::NodeStatusEnum::ePaused};
};

#endif
