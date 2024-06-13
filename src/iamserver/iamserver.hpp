/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMSERVER_HPP_
#define IAMSERVER_HPP_

#include <string>
#include <thread>
#include <vector>

#include <grpcpp/server_builder.h>

#include <aos/common/cryptoutils.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/identhandler.hpp>
#include <aos/iam/nodeinfoprovider.hpp>
#include <aos/iam/permhandler.hpp>
#include <config/config.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include "iamclient/remoteiamhandler.hpp"

/**
 * IAM GRPC server
 */
class IAMServer :
    // public services
    private iamanager::v5::IAMPublicService::Service,
    private iamanager::v5::IAMPublicIdentityService::Service,
    private iamanager::v5::IAMPublicPermissionsService::Service,
    private iamanager::v5::IAMPublicNodesService::Service,
    // protected services
    private iamanager::v5::IAMNodesService::Service,
    private iamanager::v5::IAMCertificateService::Service,
    private iamanager::v5::IAMProvisioningService::Service,
    private iamanager::v5::IAMPermissionsService::Service,
    // identhandler subject observer interface
    public aos::iam::identhandler::SubjectsObserverItf {
public:
    /**
     * Initializes IAM server instance.
     *
     * @param config server configuration.
     * @param certHandler certificate handler.
     * @param identHandler identification handler.
     * @param permHandler permission handler.
     * @param remoteHandler IAM remote handler.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param nodeInfoProvider node info provider.
     * @param provisioningMode flag indicating whether provisioning mode is active.
     */
    aos::Error Init(const Config& config, aos::iam::certhandler::CertHandlerItf& certHandler,
        aos::iam::identhandler::IdentHandlerItf* identHandler, aos::iam::permhandler::PermHandlerItf* permHandler,
        RemoteIAMHandlerItf* remoteHandler, aos::cryptoutils::CertLoader& certLoader,
        aos::crypto::x509::ProviderItf& cryptoProvider, aos::iam::NodeInfoProviderItf& nodeInfoProvider,
        bool provisioningMode);

    /**
     * Destroys IAM server.
     */
    virtual ~IAMServer();

private:
    // IAMPublicService interface
    grpc::Status GetNodeInfo(grpc::ServerContext* context, const google::protobuf::Empty* request,
        iamanager::v5::NodeInfo* response) override;
    grpc::Status GetCert(grpc::ServerContext* context, const iamanager::v5::GetCertRequest* request,
        iamanager::v5::GetCertResponse* response) override;

    // IAMPublicIdentityService interface
    grpc::Status GetSystemInfo(grpc::ServerContext* context, const google::protobuf::Empty* request,
        iamanager::v5::SystemInfo* response) override;
    grpc::Status GetSubjects(grpc::ServerContext* context, const google::protobuf::Empty* request,
        iamanager::v5::Subjects* response) override;
    grpc::Status SubscribeSubjectsChanged(grpc::ServerContext* context, const google::protobuf::Empty* request,
        grpc::ServerWriter<iamanager::v5::Subjects>* writer) override;

    // IAMPublicPermissionsService interface
    grpc::Status GetPermissions(grpc::ServerContext* context, const iamanager::v5::PermissionsRequest* request,
        iamanager::v5::PermissionsResponse* response) override;

    // IAMPublicNodesService interface
    grpc::Status GetAllNodeIDs(grpc::ServerContext* context, const google::protobuf::Empty* request,
        iamanager::v5::NodesID* response) override;
    grpc::Status GetNodeInfo(grpc::ServerContext* context, const iamanager::v5::GetNodeInfoRequest* request,
        iamanager::v5::NodeInfo* response) override;
    grpc::Status SubscribeNodeChanged(grpc::ServerContext* context, const google::protobuf::Empty* request,
        grpc::ServerWriter<iamanager::v5::NodeInfo>* writer) override;
    grpc::Status RegisterNode(grpc::ServerContext*                                                            context,
        grpc::ServerReaderWriter<::iamanager::v5::IAMIncomingMessages, ::iamanager::v5::IAMOutgoingMessages>* stream)
        override;

    // IAMNodesService interface
    grpc::Status PauseNode(grpc::ServerContext* context, const iamanager::v5::PauseNodeRequest* request,
        iamanager::v5::PauseNodeResponse* response) override;
    grpc::Status ResumeNode(grpc::ServerContext* context, const iamanager::v5::ResumeNodeRequest* request,
        iamanager::v5::ResumeNodeResponse* response) override;

    // IAMProvisioningService interface
    grpc::Status GetCertTypes(grpc::ServerContext* context, const iamanager::v5::GetCertTypesRequest* request,
        iamanager::v5::CertTypes* response) override;
    grpc::Status StartProvisioning(grpc::ServerContext* context, const iamanager::v5::StartProvisioningRequest* request,
        iamanager::v5::StartProvisioningResponse* response) override;
    grpc::Status FinishProvisioning(grpc::ServerContext* context,
        const iamanager::v5::FinishProvisioningRequest*  request,
        iamanager::v5::FinishProvisioningResponse*       response) override;
    grpc::Status Deprovision(grpc::ServerContext* context, const iamanager::v5::DeprovisionRequest* request,
        iamanager::v5::DeprovisionResponse* response) override;

    // IAMCertificateService interface
    grpc::Status CreateKey(grpc::ServerContext* context, const iamanager::v5::CreateKeyRequest* request,
        iamanager::v5::CreateKeyResponse* response) override;
    grpc::Status ApplyCert(grpc::ServerContext* context, const iamanager::v5::ApplyCertRequest* request,
        iamanager::v5::ApplyCertResponse* response) override;

    // IAMPermissionsService interface
    grpc::Status RegisterInstance(grpc::ServerContext* context, const iamanager::v5::RegisterInstanceRequest* request,
        iamanager::v5::RegisterInstanceResponse* response) override;
    grpc::Status UnregisterInstance(grpc::ServerContext* context,
        const iamanager::v5::UnregisterInstanceRequest* request, google::protobuf::Empty* response) override;

    // identhandler::SubjectsObserverItf interface
    aos::Error SubjectsChanged(const aos::Array<aos::StaticString<aos::cSubjectIDLen>>& messages) override;

    // executes command & returns error status and combined stderr & stdout
    aos::Error ExecProcess(const std::string& cmd, const std::vector<std::string>& args, std::string& output);

    // creating routines
    void CreatePublicServer(const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials);
    void RegisterPublicServices(grpc::ServerBuilder& builder);

    void CreateProtectedServer(
        const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials, bool provisionMode);
    void RegisterProtectedServices(grpc::ServerBuilder& builder, bool provisionMode);

    aos::iam::certhandler::CertHandlerItf*   mCertHandler      = nullptr;
    aos::iam::identhandler::IdentHandlerItf* mIdentHandler     = nullptr;
    aos::iam::permhandler::PermHandlerItf*   mPermHandler      = nullptr;
    RemoteIAMHandlerItf*                     mRemoteHandler    = nullptr;
    aos::iam::NodeInfoProviderItf*           mNodeInfoProvider = nullptr;

    aos::NodeInfo            mNodeInfo;
    std::vector<std::string> mFinishProvisioningCmdArgs;
    std::vector<std::string> mDiskEncryptCmdArgs;

    std::unique_ptr<grpc::Server> mPublicServer, mProtectedServer;

    std::mutex                                                mSubjectSubscriptionsLock;
    std::vector<grpc::ServerWriter<iamanager::v5::Subjects>*> mSubjectSubscriptions;
    std::vector<grpc::ServerWriter<iamanager::v5::NodeInfo>*> mNodeInfoSubscriptions;
};

#endif
