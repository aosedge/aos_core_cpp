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

#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/crypto/itf/crypto.hpp>
#include <core/common/tools/logger.hpp>
#include <core/common/tools/string.hpp>
#include <core/iam/certhandler/certhandler.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/grpchelper.hpp>
#include <common/utils/utils.hpp>

#include "iamserver.hpp"

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

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error IAMServer::Init(const config::IAMServerConfig& config, certhandler::CertHandlerItf& certHandler,
    iamclient::IdentProviderItf& identProvider, permhandler::PermHandlerItf& permHandler,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
    currentnode::CurrentNodeHandlerItf& currentNodeHandler, nodemanager::NodeManagerItf& nodeManager,
    iamclient::CertProviderItf& certProvider, provisionmanager::ProvisionManagerItf& provisionManager,
    bool provisioningMode)
{
    LOG_DBG() << "IAM Server init";

    mConfig           = config;
    mCertLoader       = &certLoader;
    mCryptoProvider   = &cryptoProvider;
    mProvisioningMode = provisioningMode;
    mCertHandler      = &certHandler;

    Error err;
    auto  nodeInfo = std::make_unique<NodeInfo>();

    if (err = currentNodeHandler.GetCurrentNodeInfo(*nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (nodeInfo->IsMainNode()) {
        nodeInfo->mIsConnected = true;

        if (err = nodeManager.SetNodeInfo(*nodeInfo); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (err = mPublicMessageHandler.Init(
            mNodeController, identProvider, permHandler, currentNodeHandler, nodeManager, certProvider);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mProtectedMessageHandler.Init(mNodeController, identProvider, permHandler, currentNodeHandler,
            nodeManager, certProvider, provisionManager);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    try {
        if (!mProvisioningMode) {
            CertInfo certInfo;

            err = certHandler.GetCert(String(mConfig.mCertStorage.c_str()), {}, {}, certInfo);
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
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    if (err = nodeManager.SubscribeListener(static_cast<iamclient::NodeInfoListenerItf&>(*this)); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error IAMServer::Start()
{
    if (mIsStarted) {
        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Start IAM server";

    if (!mProvisioningMode) {
        auto err = mCertHandler->SubscribeListener(String(mConfig.mCertStorage.c_str()), *this);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    mNodeController.Start();

    mPublicMessageHandler.Start();
    mProtectedMessageHandler.Start();

    CreatePublicServer(CorrectAddress(mConfig.mIAMPublicServerURL), mPublicCred);
    CreateProtectedServer(CorrectAddress(mConfig.mIAMProtectedServerURL), mProtectedCred);

    mIsStarted = true;

    return ErrorEnum::eNone;
}

Error IAMServer::Stop()
{
    if (!mIsStarted) {
        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Stop IAM server";

    Error err;

    if (!mProvisioningMode) {
        err = mCertHandler->UnsubscribeListener(*this);
    }

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

    return err;
}

Error IAMServer::OnStartProvisioning(const String& password)
{
    (void)password;

    LOG_DBG() << "Process on start provisioning";

    if (!mConfig.mStartProvisioningCmdArgs.empty()) {
        auto [_, err] = common::utils::ExecCommand(mConfig.mStartProvisioningCmdArgs);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error IAMServer::OnFinishProvisioning(const String& password)
{
    (void)password;

    LOG_DBG() << "Process on finish provisioning";

    if (!mConfig.mFinishProvisioningCmdArgs.empty()) {
        auto [_, err] = common::utils::ExecCommand(mConfig.mFinishProvisioningCmdArgs);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error IAMServer::OnDeprovision(const String& password)
{
    (void)password;

    LOG_DBG() << "Process on deprovisioning";

    if (!mConfig.mDeprovisionCmdArgs.empty()) {
        auto [_, err] = common::utils::ExecCommand(mConfig.mDeprovisionCmdArgs);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error IAMServer::OnEncryptDisk(const String& password)
{
    (void)password;

    LOG_DBG() << "Process on encrypt disk";

    if (!mConfig.mDiskEncryptionCmdArgs.empty()) {
        auto [_, err] = common::utils::ExecCommand(mConfig.mDiskEncryptionCmdArgs);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

void IAMServer::OnNodeInfoChanged(const NodeInfo& info)
{
    LOG_DBG() << "Process on node info changed" << Log::Field("nodeID", info.mNodeID)
              << Log::Field("state", info.mState) << Log::Field("connected", info.mIsConnected);

    mPublicMessageHandler.OnNodeInfoChanged(info);
    mProtectedMessageHandler.OnNodeInfoChanged(info);
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void IAMServer::SubjectsChanged(const Array<StaticString<cIDLen>>& subjects)
{
    mPublicMessageHandler.SubjectsChanged(subjects);
    mProtectedMessageHandler.SubjectsChanged(subjects);
}

void IAMServer::OnCertChanged(const CertInfo& info)
{
    mPublicCred = common::utils::GetTLSServerCredentials(info, *mCertLoader, *mCryptoProvider);
    mProtectedCred
        = common::utils::GetMTLSServerCredentials(info, mConfig.mCACert.c_str(), *mCertLoader, *mCryptoProvider);

    // postpone restart so it didn't block ApplyCert
    mCertChangedResult = std::async(std::launch::async, [this]() {
        sleep(1);
        Stop();
        Start();
    });
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
