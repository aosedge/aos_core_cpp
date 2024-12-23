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

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

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

static aos::Error ExecProcess(const std::string& cmd, const std::vector<std::string>& args, std::string& output)
{
    Poco::Pipe            outPipe;
    Poco::ProcessHandle   ph = Poco::Process::launch(cmd, args, nullptr, &outPipe, &outPipe);
    Poco::PipeInputStream outStream(outPipe);

    Poco::StreamCopier::copyToString(outStream, output);
    Poco::trimRightInPlace(output);

    if (int exitCode = ph.wait(); exitCode != 0) {
        aos::StaticString<aos::cMaxErrorStrLen> errStr;

        errStr.Format("Process failed: cmd=%s, code=%d", cmd.c_str(), exitCode);

        return {aos::ErrorEnum::eFailed, errStr.CStr()};
    }

    return aos::ErrorEnum::eNone;
}

static aos::Error ExecCommand(const std::string& cmdName, const std::vector<std::string>& cmdArgs)
{
    if (!cmdArgs.empty()) {
        std::string                    output;
        const std::vector<std::string> args {cmdArgs.begin() + 1, cmdArgs.end()};

        if (auto err = ExecProcess(cmdArgs[0], args, output); !err.IsNone()) {
            LOG_ERR() << cmdName.c_str() << " exec failed: output=" << output.c_str() << ", error=" << err;

            return err;
        }
    }

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error IAMServer::Init(const Config& config, aos::iam::certhandler::CertHandlerItf& certHandler,
    aos::iam::identhandler::IdentHandlerItf& identHandler, aos::iam::permhandler::PermHandlerItf& permHandler,
    aos::crypto::CertLoader& certLoader, aos::crypto::x509::ProviderItf& cryptoProvider,
    aos::iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
    aos::iam::nodemanager::NodeManagerItf& nodeManager, aos::iam::certprovider::CertProviderItf& certProvider,
    aos::iam::provisionmanager::ProvisionManagerItf& provisionManager, bool provisioningMode)
{
    LOG_DBG() << "IAM Server init";

    mConfig         = config;
    mCertLoader     = &certLoader;
    mCryptoProvider = &cryptoProvider;

    aos::Error    err;
    aos::NodeInfo nodeInfo;

    if (err = nodeInfoProvider.GetNodeInfo(nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = nodeManager.SetNodeInfo(nodeInfo); !err.IsNone()) {
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
            aos::iam::certhandler::CertInfo certInfo;

            err = certHandler.GetCertificate(aos::String(mConfig.mCertStorage.c_str()), {}, {}, certInfo);
            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            err = certHandler.SubscribeCertChanged(aos::String(mConfig.mCertStorage.c_str()), *this);
            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mPublicCred    = aos::common::utils::GetTLSServerCredentials(certInfo, certLoader, cryptoProvider);
            mProtectedCred = aos::common::utils::GetMTLSServerCredentials(
                certInfo, mConfig.mCACert.c_str(), certLoader, cryptoProvider);
        } else {
            mPublicCred    = grpc::InsecureServerCredentials();
            mProtectedCred = grpc::InsecureServerCredentials();
        }

        Start();

    } catch (const aos::common::utils::AosException& e) {
        return e.GetError();
    } catch (const std::exception& e) {
        return {aos::ErrorEnum::eFailed, e.what()};
    }

    if (err = nodeManager.SubscribeNodeInfoChange(static_cast<aos::iam::nodemanager::NodeInfoListenerItf&>(*this));
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMServer::OnStartProvisioning(const aos::String& password)
{
    (void)password;

    LOG_DBG() << "Process on start provisioning";

    return ExecCommand("Start provisioning", mConfig.mStartProvisioningCmdArgs);
}

aos::Error IAMServer::OnFinishProvisioning(const aos::String& password)
{
    (void)password;

    LOG_DBG() << "Process on finish provisioning";

    return ExecCommand("Finish provisioning", mConfig.mFinishProvisioningCmdArgs);
}

aos::Error IAMServer::OnDeprovision(const aos::String& password)
{
    (void)password;

    LOG_DBG() << "Process on deprovisioning";

    return ExecCommand("Deprovision", mConfig.mDeprovisionCmdArgs);
}

aos::Error IAMServer::OnEncryptDisk(const aos::String& password)
{
    (void)password;

    LOG_DBG() << "Process on encrypt disk";

    return ExecCommand("Encrypt disk", mConfig.mDiskEncryptionCmdArgs);
}

void IAMServer::OnNodeInfoChange(const aos::NodeInfo& info)
{
    LOG_DBG() << "Process on node info changed: nodeID=" << info.mNodeID << ", status=" << info.mStatus;

    mPublicMessageHandler.OnNodeInfoChange(info);
    mProtectedMessageHandler.OnNodeInfoChange(info);
}

void IAMServer::OnNodeRemoved(const aos::String& id)
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

aos::Error IAMServer::SubjectsChanged(const aos::Array<aos::StaticString<aos::cSubjectIDLen>>& messages)
{
    auto err = mPublicMessageHandler.SubjectsChanged(messages);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mProtectedMessageHandler.SubjectsChanged(messages); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return aos::ErrorEnum::eNone;
}

void IAMServer::OnCertChanged(const aos::iam::certhandler::CertInfo& info)
{
    mPublicCred = aos::common::utils::GetTLSServerCredentials(info, *mCertLoader, *mCryptoProvider);
    mProtectedCred
        = aos::common::utils::GetMTLSServerCredentials(info, mConfig.mCACert.c_str(), *mCertLoader, *mCryptoProvider);

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
