/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <fstream>
#include <memory>
#include <numeric>

#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/StreamCopier.h>

#include <aos/common/crypto/crypto.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/common/tools/string.hpp>
#include <aos/common/types.hpp>
#include <aos/iam/certhandler.hpp>
#include <utils/exception.hpp>
#include <utils/grpchelper.hpp>

#include "iamserver.hpp"
#include "logger/logmodule.hpp"

namespace aos::iam::iamserver {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

std::string CorrectAddress(const std::string& addr)
{
    if (addr.empty()) {
        AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "bad address");
    }

    if (addr[0] == ':') {
        return "0.0.0.0" + addr;
    }

    return addr;
}

Error ExecProcess(const std::string& cmd, const std::vector<std::string>& args, std::string& output)
{
    Poco::Pipe            outPipe;
    Poco::ProcessHandle   ph = Poco::Process::launch(cmd, args, nullptr, &outPipe, &outPipe);
    Poco::PipeInputStream outStream(outPipe);

    Poco::StreamCopier::copyToString(outStream, output);
    Poco::trimRightInPlace(output);

    if (int exitCode = ph.wait(); exitCode != 0) {
        StaticString<cMaxErrorStrLen> errStr;

        errStr.Format("Process failed: cmd=%s, code=%d", cmd.c_str(), exitCode);

        return {ErrorEnum::eFailed, errStr.CStr()};
    }

    return ErrorEnum::eNone;
}

Error ExecCommand(const std::string& cmdName, const std::vector<std::string>& cmdArgs)
{
    if (!cmdArgs.empty()) {
        std::string                    output;
        const std::vector<std::string> args {cmdArgs.begin() + 1, cmdArgs.end()};

        if (auto err = ExecProcess(cmdArgs[0], args, output); !err.IsNone()) {
            LOG_ERR() << cmdName.c_str() << " exec failed: output=" << output.c_str() << ", error=" << err;

            return err;
        }
    }

    return ErrorEnum::eNone;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error IAMServer::Init(const config::IAMServerConfig& config, certhandler::CertHandlerItf& certHandler,
    identhandler::IdentHandlerItf& identHandler, permhandler::PermHandlerItf& permHandler,
    crypto::CertLoader& certLoader, crypto::x509::ProviderItf& cryptoProvider,
    nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider, nodemanager::NodeManagerItf& nodeManager,
    certprovider::CertProviderItf& certProvider, provisionmanager::ProvisionManagerItf& provisionManager,
    bool provisioningMode)
{
    LOG_DBG() << "IAM Server init";

    mConfig         = config;
    mCertLoader     = &certLoader;
    mCryptoProvider = &cryptoProvider;

    Error err;
    auto  nodeInfo = std::make_unique<NodeInfo>();

    if (err = nodeInfoProvider.GetNodeInfo(*nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = nodeManager.SetNodeInfo(*nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mPublicMessageHandler.Init(
            mNodeController, identHandler, permHandler, nodeInfoProvider, nodeManager, certProvider);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mProtectedMessageHandler.Init(
            mNodeController, identHandler, permHandler, nodeInfoProvider, nodeManager, certProvider, provisionManager);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    try {
        if (!provisioningMode) {
            certhandler::CertInfo certInfo;

            err = certHandler.GetCertificate(String(mConfig.mCertStorage.c_str()), {}, {}, certInfo);
            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            err = certHandler.SubscribeCertChanged(String(mConfig.mCertStorage.c_str()), *this);
            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mPublicCred    = common::utils::GetTLSServerCredentials(certInfo, certLoader, cryptoProvider);
            mProtectedCred = common::utils::GetMTLSServerCredentials(
                certInfo, mConfig.mCACert.c_str(), certLoader, cryptoProvider);
        } else {
            mPublicCred    = grpc::InsecureServerCredentials();
            mProtectedCred = grpc::InsecureServerCredentials();
        }

        Start();

    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    if (err = nodeManager.SubscribeNodeInfoChange(static_cast<nodemanager::NodeInfoListenerItf&>(*this));
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error IAMServer::OnStartProvisioning(const String& password)
{
    (void)password;

    LOG_DBG() << "Process on start provisioning";

    return ExecCommand("Start provisioning", mConfig.mStartProvisioningCmdArgs);
}

Error IAMServer::OnFinishProvisioning(const String& password)
{
    (void)password;

    LOG_DBG() << "Process on finish provisioning";

    return ExecCommand("Finish provisioning", mConfig.mFinishProvisioningCmdArgs);
}

Error IAMServer::OnDeprovision(const String& password)
{
    (void)password;

    LOG_DBG() << "Process on deprovisioning";

    return ExecCommand("Deprovision", mConfig.mDeprovisionCmdArgs);
}

Error IAMServer::OnEncryptDisk(const String& password)
{
    (void)password;

    LOG_DBG() << "Process on encrypt disk";

    return ExecCommand("Encrypt disk", mConfig.mDiskEncryptionCmdArgs);
}

void IAMServer::OnNodeInfoChange(const NodeInfo& info)
{
    LOG_DBG() << "Process on node info changed: nodeID=" << info.mNodeID << ", status=" << info.mStatus;

    mPublicMessageHandler.OnNodeInfoChange(info);
    mProtectedMessageHandler.OnNodeInfoChange(info);
}

void IAMServer::OnNodeRemoved(const String& id)
{
    LOG_DBG() << "Process on node removed: nodeID=" << id;

    mPublicMessageHandler.OnNodeRemoved(id);
    mProtectedMessageHandler.OnNodeRemoved(id);
}

IAMServer::~IAMServer()
{
    Shutdown();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error IAMServer::SubjectsChanged(const Array<StaticString<cSubjectIDLen>>& messages)
{
    auto err = mPublicMessageHandler.SubjectsChanged(messages);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mProtectedMessageHandler.SubjectsChanged(messages); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

void IAMServer::OnCertChanged(const certhandler::CertInfo& info)
{
    mPublicCred = common::utils::GetTLSServerCredentials(info, *mCertLoader, *mCryptoProvider);
    mProtectedCred
        = common::utils::GetMTLSServerCredentials(info, mConfig.mCACert.c_str(), *mCertLoader, *mCryptoProvider);

    // postpone restart so it didn't block ApplyCert
    mCertChangedResult = std::async(std::launch::async, [this]() {
        sleep(1);
        Shutdown();
        Start();
    });
}

void IAMServer::Start()
{
    if (mIsStarted) {
        return;
    }

    LOG_DBG() << "IAM Server start";

    mNodeController.Start();

    mPublicMessageHandler.Start();
    mProtectedMessageHandler.Start();

    CreatePublicServer(CorrectAddress(mConfig.mIAMPublicServerURL), mPublicCred);
    CreateProtectedServer(CorrectAddress(mConfig.mIAMProtectedServerURL), mProtectedCred);

    mIsStarted = true;
}

void IAMServer::Shutdown()
{
    if (!mIsStarted) {
        return;
    }

    LOG_DBG() << "IAM Server shutdown";

    mNodeController.Close();

    mPublicMessageHandler.Close();
    mProtectedMessageHandler.Close();

    if (mPublicServer) {
        mPublicServer->Shutdown();
        mPublicServer->Wait();
    }

    if (mProtectedServer) {
        mProtectedServer->Shutdown();
        mProtectedServer->Wait();
    }

    mIsStarted = false;
}

void IAMServer::CreatePublicServer(const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials)
{
    LOG_DBG() << "Process create public server: URL=" << addr.c_str();

    grpc::ServerBuilder builder;

    builder.AddListeningPort(addr, credentials);

    mPublicMessageHandler.RegisterServices(builder);

    mPublicServer = builder.BuildAndStart();
}

void IAMServer::CreateProtectedServer(
    const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials)
{
    LOG_DBG() << "Process create protected server: URL=" << addr.c_str();

    grpc::ServerBuilder builder;

    builder.AddListeningPort(addr, credentials);

    mProtectedMessageHandler.RegisterServices(builder);

    mProtectedServer = builder.BuildAndStart();
}

} // namespace aos::iam::iamserver
