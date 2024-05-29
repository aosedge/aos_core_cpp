/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "iamserver.hpp"

#include <memory>

#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>

#include <aos/common/crypto.hpp>
#include <aos/common/cryptoutils.hpp>
#include <aos/common/tools/string.hpp>
#include <aos/common/types.hpp>
#include <aos/iam/certhandler.hpp>
#include <utils/exception.hpp>
#include <utils/grpchelper.hpp>

#include "log.hpp"

using namespace aos;
using namespace aos::iam;

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cDiscEncryptionType = "diskencryption";
constexpr auto cIamAPIVersion      = 4;
const int      cMaxSubjectsCount   = 10;

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

const Array<uint8_t> ConvertByteArrayToAOS(const std::string& arr)
{
    return {reinterpret_cast<const uint8_t*>(arr.c_str()), arr.length()};
}

RetWithError<std::string> ConvertSerialToProto(const StaticArray<uint8_t, crypto::cSerialNumSize>& src)
{
    StaticString<crypto::cSerialNumStrLen> result;

    auto err = result.ByteArrayToHex(src);

    return {result.Get(), err};
}

template <size_t Size>
void ConvertToProto(const Array<StaticString<Size>>& src, google::protobuf::RepeatedPtrField<std::string>& dst)
{
    for (const auto& val : src) {
        dst.Add(val.CStr());
    }
}

aos::InstanceIdent ConvertToAOS(const iamanager::v5::InstanceIdent& val)
{
    aos::InstanceIdent result;

    result.mServiceID = val.service_id().c_str();
    result.mSubjectID = val.subject_id().c_str();
    result.mInstance  = val.instance();

    return result;
}

void ConvertToProto(const Array<StaticString<cSubjectIDLen>>& src, iamanager::v5::Subjects& dst)
{
    dst.clear_subjects();

    for (const auto& subject : src) {
        dst.add_subjects(subject.CStr());
    }
}

static const std::string CorrectAddress(const std::string& addr)
{
    if (addr.empty()) {
        throw aos::common::utils::AosException("bad address");
    }

    if (addr[0] == ':') {
        return "0.0.0.0" + addr;
    }

    return addr;
}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error IAMServer::Init(const Config& config, certhandler::CertHandlerItf& certHandler,
    identhandler::IdentHandlerItf* identHandler, permhandler::PermHandlerItf* permHandler,
    RemoteIAMHandlerItf* remoteHandler, cryptoutils::CertLoader& certLoader, crypto::x509::ProviderItf& cryptoProvider,
    aos::iam::NodeInfoProviderItf& nodeInfoProvider, bool provisioningMode)
{
    mCertHandler               = &certHandler;
    mIdentHandler              = identHandler;
    mPermHandler               = permHandler;
    mRemoteHandler             = remoteHandler;
    mNodeInfoProvider          = &nodeInfoProvider;
    mFinishProvisioningCmdArgs = config.mFinishProvisioningCmdArgs;
    mDiskEncryptCmdArgs        = config.mDiskEncryptionCmdArgs;

    if (const auto err = nodeInfoProvider.GetNodeInfo(mNodeInfo); !err.IsNone()) {
        LOG_ERR() << "Failed to get node info: " << AOS_ERROR_WRAP(err);

        return ErrorEnum::eFailed;
    }

    try {
        std::shared_ptr<grpc::ServerCredentials> publicOpt, protectedOpt;

        if (!provisioningMode) {
            certhandler::CertInfo certInfo;

            auto err = mCertHandler->GetCertificate(aos::String(config.mCertStorage.c_str()), {}, {}, certInfo);
            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            publicOpt    = aos::common::utils::GetTLSServerCredentials(certInfo, certLoader, cryptoProvider);
            protectedOpt = aos::common::utils::GetMTLSServerCredentials(
                certInfo, config.mCACert.c_str(), certLoader, cryptoProvider);
        } else {
            publicOpt    = grpc::InsecureServerCredentials();
            protectedOpt = grpc::InsecureServerCredentials();
        }

        CreatePublicServer(config.mIAMPublicServerURL, publicOpt);
        CreateProtectedServer(config.mIAMProtectedServerURL, protectedOpt, provisioningMode);
    } catch (const std::exception& e) {
        LOG_ERR() << "Init error: " << e.what();

        return ErrorEnum::eFailed;
    }

    return aos::ErrorEnum::eNone;
}

IAMServer::~IAMServer()
{
    if (mPublicServer) {
        mPublicServer->Shutdown();
        mPublicServer->Wait();
    }

    if (mProtectedServer) {
        mProtectedServer->Shutdown();
        mProtectedServer->Wait();
    }
}

/***********************************************************************************************************************
 * IAMPublicService implementation
 **********************************************************************************************************************/

grpc::Status IAMServer::GetAPIVersion(
    grpc::ServerContext* context, const google::protobuf::Empty* request, iamanager::v5::APIVersion* response)
{
    (void)context;
    (void)request;

    LOG_DBG() << "Process get API version";

    response->set_version(cIamAPIVersion);

    return grpc::Status::OK;
}

grpc::Status IAMServer::GetNodeInfo(
    grpc::ServerContext* context, const google::protobuf::Empty* request, iamanager::v5::NodeInfo* response)
{
    (void)context;
    (void)request;

    LOG_DBG() << "Process get node info";

    response->set_id(mNodeInfo.mID.CStr());
    response->set_type(mNodeInfo.mType.CStr());

    return grpc::Status::OK;
}

grpc::Status IAMServer::GetCert(grpc::ServerContext* context, const iamanager::v5::GetCertRequest* request,
    iamanager::v5::GetCertResponse* response)
{
    (void)context;

    LOG_DBG() << "Process get cert request: type=" << request->type().c_str()
              << ", serial=" << request->serial().c_str();

    response->set_type(request->type());

    auto issuer = ConvertByteArrayToAOS(request->issuer());

    StaticArray<uint8_t, crypto::cSerialNumSize> serial;

    auto err = String(request->serial().c_str()).HexToByteArray(serial);
    if (!err.IsNone()) {
        LOG_ERR() << "Serial conversion failed: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "Serial conversion failed");
    }

    certhandler::CertInfo certInfo;

    err = mCertHandler->GetCertificate(request->type().c_str(), issuer, serial, certInfo);
    if (!err.IsNone()) {
        LOG_ERR() << "Get certificate error: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "Get certificate error");
    }

    response->set_key_url(certInfo.mKeyURL.CStr());
    response->set_cert_url(certInfo.mCertURL.CStr());

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMPublicIdentityService implementation
 **********************************************************************************************************************/

grpc::Status IAMServer::GetSystemInfo(
    grpc::ServerContext* context, const google::protobuf::Empty* request, iamanager::v5::SystemInfo* response)
{
    (void)context;
    (void)request;

    LOG_DBG() << "Process get system info";

    auto [systemID, err1] = mIdentHandler->GetSystemID();

    if (!err1.IsNone()) {
        LOG_DBG() << "Get system ID error: " << err1;

        return grpc::Status(grpc::StatusCode::INTERNAL, "Get system ID error");
    }

    auto [boardModel, err2] = mIdentHandler->GetUnitModel();
    if (!err2.IsNone()) {
        LOG_DBG() << "Get board model error: " << err2;

        return grpc::Status(grpc::StatusCode::INTERNAL, "Get board model error");
    }

    response->set_system_id(systemID.CStr());
    response->set_unit_model(boardModel.CStr());

    return grpc::Status::OK;
}

grpc::Status IAMServer::GetSubjects(
    grpc::ServerContext* context, const google::protobuf::Empty* request, iamanager::v5::Subjects* response)
{
    (void)context;
    (void)request;

    LOG_DBG() << "Process get subjects";

    aos::StaticArray<aos::StaticString<aos::cSubjectIDLen>, cMaxSubjectsCount> subjects;

    auto err = mIdentHandler->GetSubjects(subjects);
    if (!err.IsNone()) {
        LOG_DBG() << "Get subjects error: " << err;

        return grpc::Status(grpc::StatusCode::INTERNAL, "Get subjects error");
    }

    for (const auto& subj : subjects) {
        response->add_subjects(subj.CStr());
    }

    return grpc::Status::OK;
}

grpc::Status IAMServer::SubscribeSubjectsChanged(grpc::ServerContext* context, const google::protobuf::Empty* request,
    grpc::ServerWriter<iamanager::v5::Subjects>* writer)
{
    (void)context;
    (void)request;

    std::lock_guard<std::mutex> lock(mSubjectSubscriptionsLock);

    mSubjectSubscriptions.push_back(writer);

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMPublicPermissionsService implementation
 **********************************************************************************************************************/

grpc::Status IAMServer::GetPermissions(grpc::ServerContext* context, const iamanager::v5::PermissionsRequest* request,
    iamanager::v5::PermissionsResponse* response)
{
    (void)context;

    LOG_DBG() << "Process get permissions: funcServerID" << request->functional_server_id().c_str();

    InstanceIdent                                                                   aosInstanceIdent;
    StaticArray<permhandler::PermKeyValue, permhandler::cServicePermissionMaxCount> aosInstancePerm;

    auto err = mPermHandler->GetPermissions(
        request->secret().c_str(), request->functional_server_id().c_str(), aosInstanceIdent, aosInstancePerm);
    if (!err.IsNone()) {
        LOG_DBG() << "GetPermissions error: " << err;

        return grpc::Status(grpc::StatusCode::INTERNAL, "GetPermissions error");
    }

    iamanager::v5::InstanceIdent instanceIdent;
    iamanager::v5::Permissions   permissions;

    instanceIdent.set_service_id(aosInstanceIdent.mServiceID.CStr());
    instanceIdent.set_subject_id(aosInstanceIdent.mSubjectID.CStr());
    instanceIdent.set_instance(aosInstanceIdent.mInstance);

    for (const auto& [key, val] : aosInstancePerm) {
        (*permissions.mutable_permissions())[key.CStr()] = val.CStr();
    }

    *response->mutable_instance()    = instanceIdent;
    *response->mutable_permissions() = permissions;

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMPublicNodesService implementation
 **********************************************************************************************************************/

grpc::Status IAMServer::GetAllNodeIDs(
    grpc::ServerContext* context, const google::protobuf::Empty* request, iamanager::v5::NodesID* response)
{
    (void)context;
    (void)request;

    LOG_DBG() << "Process get all node IDs";

    response->add_ids(mNodeInfo.mID.CStr());

    if (!mRemoteHandler) {
        return grpc::Status::OK;
    }

    const auto& remoteNodes = mRemoteHandler->GetRemoteNodes();

    for (const auto& node : remoteNodes) {
        response->add_ids(node.CStr());
    }

    return grpc::Status::OK;
}

grpc::Status IAMServer::GetNodeInfo(
    grpc::ServerContext* context, const iamanager::v5::GetNodeInfoRequest* request, iamanager::v5::NodeInfo* response)
{
    (void)context;
    (void)request;
    (void)response;

    return grpc::Status::CANCELLED;
}

grpc::Status IAMServer::SubscribeNodeChanged(grpc::ServerContext* context, const google::protobuf::Empty* request,
    grpc::ServerWriter<iamanager::v5::NodeInfo>* writer)
{
    (void)context;
    (void)request;

    mNodeInfoSubscriptions.push_back(writer);

    return grpc::Status::OK;
}

grpc::Status IAMServer::RegisterNode(grpc::ServerContext*                                                 context,
    grpc::ServerReaderWriter<::iamanager::v5::IAMIncomingMessages, ::iamanager::v5::IAMOutgoingMessages>* stream)
{
    (void)context;
    (void)stream;

    return grpc::Status::CANCELLED;
}

/***********************************************************************************************************************
 * IAMNodesService implementation
 **********************************************************************************************************************/
grpc::Status IAMServer::PauseNode(grpc::ServerContext* context, const iamanager::v5::PauseNodeRequest* request,
    iamanager::v5::PauseNodeResponse* response)
{
    (void)context;
    (void)request;
    (void)response;

    return grpc::Status::CANCELLED;
}

grpc::Status IAMServer::ResumeNode(grpc::ServerContext* context, const iamanager::v5::ResumeNodeRequest* request,
    iamanager::v5::ResumeNodeResponse* response)
{
    (void)context;
    (void)request;
    (void)response;

    return grpc::Status::CANCELLED;
}

/***********************************************************************************************************************
 * IAMProvisioningService implementation
 **********************************************************************************************************************/
grpc::Status IAMServer::GetCertTypes(
    grpc::ServerContext* context, const iamanager::v5::GetCertTypesRequest* request, iamanager::v5::CertTypes* response)
{
    (void)context;

    LOG_DBG() << "Process get cert types: nodeID=" << request->node_id().c_str();

    const auto& nodeID = request->node_id();
    Error       err    = ErrorEnum::eNone;
    StaticArray<StaticString<certhandler::cCertTypeLen>, certhandler::cIAMCertModulesMaxCount> certTypes;

    if (aos::String(nodeID.c_str()) == mNodeInfo.mID.CStr() || nodeID.empty()) {
        err = mCertHandler->GetCertTypes(certTypes);
    } else if (mRemoteHandler) {
        err = mRemoteHandler->GetCertTypes(nodeID.c_str(), certTypes);
    } else {
        LOG_DBG() << "unknown node ID";

        return grpc::Status(grpc::StatusCode::INTERNAL, "unknown node ID");
    }

    if (!err.IsNone()) {
        LOG_ERR() << "Get certificate types error: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "Get certificate types error");
    }

    ConvertToProto(certTypes, *response->mutable_types());
    return grpc::Status::OK;
}

grpc::Status IAMServer::StartProvisioning(grpc::ServerContext* context,
    const iamanager::v5::StartProvisioningRequest* request, iamanager::v5::StartProvisioningResponse* response)
{
    (void)context;
    (void)request;
    (void)response;

    return grpc::Status::CANCELLED;
}

grpc::Status IAMServer::FinishProvisioning(grpc::ServerContext* context,
    const iamanager::v5::FinishProvisioningRequest* request, iamanager::v5::FinishProvisioningResponse* response)
{
    (void)context;
    (void)request;
    (void)response;

    LOG_DBG() << "Process finish provisioning request";

    Error err = ErrorEnum::eNone;

    if (mRemoteHandler) {
        for (const auto& node : mRemoteHandler->GetRemoteNodes()) {
            auto nodeErr = mRemoteHandler->FinishProvisioning(node);
            if (!nodeErr.IsNone() && err.IsNone()) {
                err = nodeErr;
            }
        }
    }

    if (!mFinishProvisioningCmdArgs.empty()) {
        std::string                    output;
        const std::vector<std::string> args {mFinishProvisioningCmdArgs.begin() + 1, mFinishProvisioningCmdArgs.end()};

        auto execErr = ExecProcess(mFinishProvisioningCmdArgs[0], args, output);
        if (!execErr.IsNone() && err.IsNone()) {
            err = execErr;

            LOG_ERR() << "Exec error: message = " << output.c_str() << ", err = " << AOS_ERROR_WRAP(err);
        }
    }

    if (!err.IsNone()) {
        LOG_ERR() << "Finish provisioning error: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "Finish provisioning error");
    }

    return grpc::Status::OK;
}

grpc::Status IAMServer::Deprovision(grpc::ServerContext* context, const iamanager::v5::DeprovisionRequest* request,
    iamanager::v5::DeprovisionResponse* response)
{
    (void)context;
    (void)request;
    (void)response;

    return grpc::Status::CANCELLED;
}

/***********************************************************************************************************************
 * IAMCertificateService implementation
 **********************************************************************************************************************/

grpc::Status IAMServer::CreateKey(grpc::ServerContext* context, const iamanager::v5::CreateKeyRequest* request,
    iamanager::v5::CreateKeyResponse* response)
{
    (void)context;

    const auto                 nodeID   = request->node_id();
    const auto                 certType = String(request->type().c_str());
    StaticString<cSystemIDLen> subject  = request->subject().c_str();
    const auto                 password = String(request->password().c_str());

    LOG_DBG() << "Process create key request: type=" << certType << ", nodeID=" << nodeID.c_str()
              << ", subject=" << subject;

    if (subject.IsEmpty() && !mIdentHandler) {
        LOG_ERR() << "Subject can't be empty";

        return grpc::Status(grpc::StatusCode::INTERNAL, "Subject can't be empty");
    }

    Error err = ErrorEnum::eNone;

    if (subject.IsEmpty() && mIdentHandler) {
        Tie(subject, err) = mIdentHandler->GetSystemID();
        if (!err.IsNone()) {
            LOG_ERR() << "Getting system ID error: " << AOS_ERROR_WRAP(err);

            return grpc::Status(grpc::StatusCode::INTERNAL, "GetSystemID returned error");
        }
    }

    StaticString<crypto::cCSRPEMLen> csr;

    if (aos::String(nodeID.c_str()) == mNodeInfo.mID || nodeID.empty()) {
        err = mCertHandler->CreateKey(certType, subject, password, csr);
    } else if (mRemoteHandler) {
        err = mRemoteHandler->CreateKey(nodeID.c_str(), certType, subject, password, csr);
    } else {
        LOG_DBG() << "unknown node ID";

        return grpc::Status(grpc::StatusCode::INTERNAL, "unknown node ID");
    }

    if (!err.IsNone()) {
        LOG_ERR() << "Create key request failed: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "Create key request failed");
    }

    response->set_node_id(nodeID);
    response->set_type(certType.CStr());
    response->set_csr(csr.CStr());

    return grpc::Status::OK;
}

grpc::Status IAMServer::ApplyCert(grpc::ServerContext* context, const iamanager::v5::ApplyCertRequest* request,
    iamanager::v5::ApplyCertResponse* response)
{
    (void)context;

    const auto nodeID   = request->node_id();
    const auto certType = String(request->type().c_str());
    const auto pemCert  = String(request->cert().c_str());

    LOG_DBG() << "Process apply cert request: type=" << certType << ", nodeID=" << nodeID.c_str();

    Error                 err = ErrorEnum::eNone;
    certhandler::CertInfo certInfo;

    if (aos::String(nodeID.c_str()) == mNodeInfo.mID || nodeID.empty()) {
        err = mCertHandler->ApplyCertificate(certType, pemCert, certInfo);
    } else if (mRemoteHandler) {
        err = mRemoteHandler->ApplyCertificate(nodeID.c_str(), certType, pemCert, certInfo);
    } else {
        LOG_DBG() << "unknown node ID";

        return grpc::Status(grpc::StatusCode::INTERNAL, "unknown node ID");
    }

    if (!err.IsNone()) {
        LOG_ERR() << "Apply cert request failed: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "apply cert request failed");
    }

    std::string serial;

    Tie(serial, err) = ConvertSerialToProto(certInfo.mSerial);
    if (!err.IsNone()) {
        LOG_ERR() << "Serial conversion problem: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "serial conversion problem");
    }

    response->set_node_id(nodeID);
    response->set_type(certType.CStr());
    response->set_cert_url(certInfo.mCertURL.CStr());
    response->set_serial(serial);

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMPermissionsService implementation
 **********************************************************************************************************************/

grpc::Status IAMServer::RegisterInstance(grpc::ServerContext* context,
    const iamanager::v5::RegisterInstanceRequest* request, iamanager::v5::RegisterInstanceResponse* response)
{
    (void)context;

    aos::Error err         = aos::ErrorEnum::eNone;
    const auto aosInstance = ConvertToAOS(request->instance());

    LOG_DBG() << "Process register instance: serviceID=" << aosInstance.mServiceID
              << ", subjectID=" << aosInstance.mSubjectID << ", instance=" << aosInstance.mInstance;

    // Convert permissions
    StaticArray<permhandler::FunctionalServicePermissions, cMaxNumServices> aosPermissions;

    for (const auto& [service, permissions] : request->permissions()) {
        err = aosPermissions.PushBack({});
        if (!err.IsNone()) {
            LOG_ERR() << "Error allocating permissions: " << AOS_ERROR_WRAP(err);

            return grpc::Status(grpc::StatusCode::INTERNAL, "Permissions allocation problem");
        }

        permhandler::FunctionalServicePermissions& servicePerm = aosPermissions.Back().mValue;
        servicePerm.mName                                      = service.c_str();

        for (const auto& [key, val] : permissions.permissions()) {
            err = servicePerm.mPermissions.PushBack({key.c_str(), val.c_str()});
            if (!err.IsNone()) {
                LOG_ERR() << "Error allocating permissions: " << AOS_ERROR_WRAP(err);

                return grpc::Status(grpc::StatusCode::INTERNAL, "Permissions allocation problem");
            }
        }
    }

    StaticString<uuid::cUUIDLen> secret;

    Tie(secret, err) = mPermHandler->RegisterInstance(aosInstance, aosPermissions);

    if (!err.IsNone()) {
        LOG_ERR() << "Register instance error: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "Register instance error");
    }

    response->set_secret(secret.CStr());

    return grpc::Status::OK;
}

grpc::Status IAMServer::UnregisterInstance(grpc::ServerContext* context,
    const iamanager::v5::UnregisterInstanceRequest* request, google::protobuf::Empty* response)
{
    (void)context;
    (void)response;

    const auto instance = ConvertToAOS(request->instance());

    LOG_DBG() << "Process unregister instance: serviceID=" << instance.mServiceID
              << ", subjectID=" << instance.mSubjectID << ", instance=" << instance.mInstance;

    auto err = mPermHandler->UnregisterInstance(instance);
    if (!err.IsNone()) {
        LOG_ERR() << "Unregister instance error: " << AOS_ERROR_WRAP(err);

        return grpc::Status(grpc::StatusCode::INTERNAL, "Unregister instance error");
    }

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * SubjectsObserverItf implementation
 **********************************************************************************************************************/

Error IAMServer::SubjectsChanged(const Array<StaticString<cSubjectIDLen>>& messages)
{
    std::lock_guard<std::mutex> lock(mSubjectSubscriptionsLock);
    iamanager::v5::Subjects     subjects;

    ConvertToProto(messages, subjects);

    for (const auto writer : mSubjectSubscriptions) {
        writer->Write(subjects);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Auxiliary private methods
 **********************************************************************************************************************/

Error IAMServer::ExecProcess(const std::string& cmd, const std::vector<std::string>& args, std::string& output)
{
    Poco::Pipe outPipe;
    Poco::Pipe errPipe;

    Poco::ProcessHandle ph = Poco::Process::launch(cmd, args, nullptr, &outPipe, &errPipe);

    outPipe.close(Poco::Pipe::CLOSE_WRITE);
    errPipe.close(Poco::Pipe::CLOSE_WRITE);

    Poco::PipeInputStream outStream(outPipe);
    Poco::PipeInputStream errStream(errPipe);

    std::string line;

    while (std::getline(outStream, line)) {
        output += line + "\n";
    }

    while (std::getline(errStream, line)) {
        output += line + "\n";
    }

    int exitCode = ph.wait();

    return exitCode == 0 ? ErrorEnum::eNone : ErrorEnum::eFailed;
}

void IAMServer::CreatePublicServer(const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials)
{
    grpc::ServerBuilder builder;

    builder.AddListeningPort(CorrectAddress(addr), credentials);

    RegisterPublicServices(builder);

    mPublicServer = builder.BuildAndStart();
}

void IAMServer::RegisterPublicServices(grpc::ServerBuilder& builder)
{
    using namespace iamanager::v5;

    builder.RegisterService(static_cast<IAMPublicService::Service*>(this));

    if (mIdentHandler != nullptr) {
        builder.RegisterService(static_cast<IAMPublicIdentityService::Service*>(this));
    }

    if (mPermHandler != nullptr) {
        builder.RegisterService(static_cast<IAMPublicPermissionsService::Service*>(this));
    }
}

void IAMServer::CreateProtectedServer(
    const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials, bool provisionMode)
{
    grpc::ServerBuilder builder;

    builder.AddListeningPort(CorrectAddress(addr), credentials);

    RegisterPublicServices(builder);
    RegisterProtectedServices(builder, provisionMode);

    mProtectedServer = builder.BuildAndStart();
}

void IAMServer::RegisterProtectedServices(grpc::ServerBuilder& builder, bool provisionMode)
{
    using namespace iamanager::v5;

    builder.RegisterService(static_cast<IAMCertificateService::Service*>(this));

    if (provisionMode) {
        builder.RegisterService(static_cast<IAMProvisioningService::Service*>(this));
    }

    if (mPermHandler != nullptr) {
        builder.RegisterService(static_cast<IAMPermissionsService::Service*>(this));
    }
}
