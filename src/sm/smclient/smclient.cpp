/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/pbconvert/sm.hpp>
#include <sm/logger/logmodule.hpp>

#include "smclient.hpp"

namespace aos::sm::smclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error SMClient::Init(const Config& config, const std::string& nodeID,
    aos::common::iamclient::TLSCredentialsItf& tlsCredentials, aos::iamclient::CertProviderItf& certProvider,
    launcher::RuntimeInfoProviderItf&         runtimeInfoProvider,
    resourcemanager::ResourceInfoProviderItf& resourceInfoProvider, nodeconfig::NodeConfigHandlerItf& nodeConfigHandler,
    launcher::LauncherItf& launcher, logging::LogProviderItf& logProvider,
    networkmanager::NetworkManagerItf& networkManager, aos::monitoring::MonitoringItf& monitoring,
    aos::instancestatusprovider::ProviderItf& instanceStatusProvider, bool secureConnection)
{
    LOG_DBG() << "Init SM client";

    mConfig                 = config;
    mNodeID                 = nodeID;
    mTLSCredentials         = &tlsCredentials;
    mCertProvider           = &certProvider;
    mRuntimeInfoProvider    = &runtimeInfoProvider;
    mResourceInfoProvider   = &resourceInfoProvider;
    mNodeConfigHandler      = &nodeConfigHandler;
    mLauncher               = &launcher;
    mLogProvider            = &logProvider;
    mNetworkManager         = &networkManager;
    mMonitoring             = &monitoring;
    mInstanceStatusProvider = &instanceStatusProvider;
    mSecureConnection       = secureConnection;

    return ErrorEnum::eNone;
}

Error SMClient::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start SM client";

    if (!mStopped) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "client already started"));
    }

    if (mSecureConnection) {
        auto [creds, err] = mTLSCredentials->GetMTLSClientCredentials(mConfig.mCertStorage.c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't get MTLS client credentials"));
        }

        mCredentials = std::move(creds);

        if (err = mCertProvider->SubscribeListener(mConfig.mCertStorage.c_str(), *this); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't subscribe to certificate changes"));
        }
    } else {
        auto [creds, err] = mTLSCredentials->GetTLSClientCredentials();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't get TLS client credentials"));
        }

        mCredentials = std::move(creds);
    }

    mStopped = false;

    mConnectionThread = std::thread(&SMClient::ConnectionLoop, this);

    return ErrorEnum::eNone;
}

Error SMClient::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Stop SM client";

        if (mStopped) {
            return ErrorEnum::eNone;
        }

        mStopped = true;
        mStoppedCV.notify_all();

        if (mSecureConnection) {
            mCertProvider->UnsubscribeListener(*this);
        }

        if (mCtx) {
            mCtx->TryCancel();
        }
    }

    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }

    return ErrorEnum::eNone;
}

void SMClient::OnCertChanged(const CertInfo& info)
{
    (void)info;

    std::lock_guard lock {mMutex};

    LOG_INF() << "Certificate changed";

    auto [creds, err] = mTLSCredentials->GetMTLSClientCredentials(mConfig.mCertStorage.c_str());
    if (!err.IsNone()) {
        LOG_ERR() << "Can't get client credentials: err=" << err;

        return;
    }

    mCredentials = std::move(creds);

    LOG_DBG() << "Credentials updated";
}

Error SMClient::SendAlert(const AlertVariant& alert)
{
    (void)alert;

    return ErrorEnum::eNone;
}

Error SMClient::SendMonitoringData(const aos::monitoring::NodeMonitoringData& monitoringData)
{
    (void)monitoringData;

    return ErrorEnum::eNone;
}

Error SMClient::SendLog(const PushLog& log)
{
    (void)log;

    return ErrorEnum::eNone;
}

Error SMClient::SendNodeInstancesStatuses(const Array<aos::InstanceStatus>& statuses)
{
    std::lock_guard lock {mMutex};

    if (!mStream) {
        return Error(ErrorEnum::eFailed, "stream not available");
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       nodeStatus = *outgoingMsg.mutable_node_instances_status();

    for (const auto& status : statuses) {
        common::pbconvert::ConvertToProto(status, *nodeStatus.add_instances());
    }

    if (!mStream->Write(outgoingMsg)) {
        return Error(ErrorEnum::eFailed, "can't send node instances statuses");
    }

    return ErrorEnum::eNone;
}

Error SMClient::SendUpdateInstancesStatuses(const Array<aos::InstanceStatus>& statuses)
{
    std::lock_guard lock {mMutex};

    if (!mStream) {
        return Error(ErrorEnum::eFailed, "stream not available");
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       updateStatus = *outgoingMsg.mutable_update_instances_status();

    for (const auto& status : statuses) {
        common::pbconvert::ConvertToProto(status, *updateStatus.add_instances());
    }

    if (!mStream->Write(outgoingMsg)) {
        return Error(ErrorEnum::eFailed, "can't send update instances statuses");
    }

    return ErrorEnum::eNone;
}

Error SMClient::GetBlobsInfo(const Array<StaticString<oci::cDigestLen>>& digests, Array<StaticString<cURLLen>>& urls)
{
    (void)digests;
    (void)urls;

    return ErrorEnum::eNone;
}

Error SMClient::SubscribeListener(aos::cloudconnection::ConnectionListenerItf& listener)
{
    (void)listener;

    return ErrorEnum::eNone;
}

Error SMClient::UnsubscribeListener(aos::cloudconnection::ConnectionListenerItf& listener)
{
    (void)listener;

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::unique_ptr<grpc::ClientContext> SMClient::CreateClientContext()
{
    return std::make_unique<grpc::ClientContext>();
}

SMClient::StubPtr SMClient::CreateStub(
    const std::string& url, const std::shared_ptr<grpc::ChannelCredentials>& credentials)
{
    auto channel = grpc::CreateCustomChannel(url, credentials, grpc::ChannelArguments());
    if (!channel) {
        LOG_ERR() << "Can't create client channel";

        return nullptr;
    }

    return smproto::SMService::NewStub(channel);
}

bool SMClient::SendSMInfo()
{
    LOG_INF() << "Send SM info";

    if (!mStream) {
        return false;
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       smInfo = *outgoingMsg.mutable_sm_info();

    smInfo.set_node_id(mNodeID);

    auto runtimes = std::make_unique<RuntimeInfoArray>();
    if (auto err = mRuntimeInfoProvider->GetRuntimesInfos(*runtimes); !err.IsNone()) {
        LOG_ERR() << "Can't get runtimes info: err=" << err;

        return false;
    }

    for (const auto& runtime : *runtimes) {
        common::pbconvert::ConvertToProto(runtime, *smInfo.add_runtimes());
    }

    auto resources = std::make_unique<StaticArray<resourcemanager::ResourceInfo, cMaxNumNodeResources>>();
    if (auto err = mResourceInfoProvider->GetResourcesInfos(*resources); !err.IsNone()) {
        LOG_ERR() << "Can't get resources info: err=" << err;

        return false;
    }

    for (const auto& resource : *resources) {
        common::pbconvert::ConvertToProto(
            static_cast<const resourcemanager::ResourceInfo&>(resource), *smInfo.add_resources());
    }

    return mStream->Write(outgoingMsg);
}

bool SMClient::SendNodeInstancesStatus()
{
    LOG_INF() << "Send node instances status";

    if (!mStream) {
        return false;
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       nodeStatus = *outgoingMsg.mutable_node_instances_status();

    auto statuses = std::make_unique<InstanceStatusArray>();
    if (auto err = mInstanceStatusProvider->GetInstancesStatuses(*statuses); !err.IsNone()) {
        LOG_ERR() << "Can't get instances statuses: err=" << err;

        return false;
    }

    for (const auto& status : *statuses) {
        common::pbconvert::ConvertToProto(status, *nodeStatus.add_instances());
    }

    return mStream->Write(outgoingMsg);
}

bool SMClient::RegisterSM(const std::string& url)
{
    std::lock_guard lock {mMutex};

    if (mStopped) {
        return false;
    }

    mStub = CreateStub(url, mCredentials);
    if (!mStub) {
        LOG_ERR() << "Can't create stub";

        return false;
    }

    mCtx = CreateClientContext();

    mStream = mStub->RegisterSM(mCtx.get());
    if (!mStream) {
        LOG_ERR() << "Can't register SM";

        return false;
    }

    LOG_INF() << "Connection established";

    return true;
}

void SMClient::ConnectionLoop() noexcept
{
    LOG_DBG() << "SM client connection thread started";

    while (true) {
        LOG_DBG() << "Connecting to SM server...";

        if (RegisterSM(mConfig.mCMServerURL)) {
            if (!SendSMInfo()) {
                LOG_ERR() << "Can't send SM info";
            } else if (!SendNodeInstancesStatus()) {
                LOG_ERR() << "Can't send node instances status";
            }

            LOG_DBG() << "SM client connection closed";
        }

        std::unique_lock lock {mMutex};

        mStoppedCV.wait_for(
            lock, std::chrono::nanoseconds(mConfig.mCMReconnectTimeout.Nanoseconds()), [this] { return mStopped; });

        if (mStopped) {
            break;
        }
    }

    LOG_DBG() << "SM client connection thread stopped";
}

} // namespace aos::sm::smclient
