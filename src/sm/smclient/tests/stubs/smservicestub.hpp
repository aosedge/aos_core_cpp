/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_SMCLIENT_TESTS_STUBS_SMSERVICESTUB_HPP_
#define AOS_SM_SMCLIENT_TESTS_STUBS_SMSERVICESTUB_HPP_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <grpcpp/server_builder.h>

#include <common/utils/grpchelper.hpp>

#include <servicemanager/v5/network.grpc.pb.h>
#include <servicemanager/v5/servicemanager.grpc.pb.h>

namespace smproto = servicemanager::v5;

/**
 * Test stub for SMService v5.
 */
class SMServiceStub final : public smproto::SMService::Service, public smproto::NetworkService::Service {
public:
    SMServiceStub(const std::string& url) { mServer = CreateServer(url, grpc::InsecureServerCredentials()); }

    ~SMServiceStub()
    {
        if (mCtx) {
            mCtx->TryCancel();
        }
    }

    grpc::Status RegisterSM(grpc::ServerContext*                                            context,
        grpc::ServerReaderWriter<smproto::SMIncomingMessages, smproto::SMOutgoingMessages>* stream) override
    {
        mStream = stream;
        mCtx    = context;

        {
            std::lock_guard lock {mLock};

            mRegistered = true;
            mCV.notify_all();
        }

        smproto::SMOutgoingMessages msg;

        while (stream->Read(&msg)) {
            if (msg.has_sm_info()) {
                std::lock_guard lock {mLock};

                OnSMInfo(msg.sm_info());
                mSMInfoReceived = true;
                mCV.notify_all();
            } else if (msg.has_node_instances_status()) {
                std::lock_guard lock {mLock};

                OnNodeInstancesStatus(msg.node_instances_status());
                mNodeInstancesStatusReceived = true;
                mCV.notify_all();
            } else if (msg.has_update_instances_status()) {
                std::lock_guard lock {mLock};

                OnUpdateInstancesStatus(msg.update_instances_status());
                mUpdateInstancesStatusReceived = true;
                mCV.notify_all();
            } else if (msg.has_instant_monitoring()) {
                std::lock_guard lock {mLock};

                OnInstantMonitoring(msg.instant_monitoring());
                mInstantMonitoringReceived = true;
                mCV.notify_all();
            } else if (msg.has_alert()) {
                std::lock_guard lock {mLock};

                OnAlert(msg.alert());
                mAlertReceived = true;
                mCV.notify_all();
            } else if (msg.has_node_config_status()) {
                std::lock_guard lock {mLock};

                OnNodeConfigStatus(msg.node_config_status());
                mNodeConfigStatusReceived = true;
                mCV.notify_all();
            } else if (msg.has_average_monitoring()) {
                std::lock_guard lock {mLock};

                OnAverageMonitoring(msg.average_monitoring());
                mAverageMonitoringReceived = true;
                mCV.notify_all();
            } else if (msg.has_log()) {
                std::lock_guard lock {mLock};

                OnLogData(msg.log());
                mLogDataReceived = true;
                mCV.notify_all();
            }
        }

        mStream = nullptr;
        mCtx    = nullptr;

        return grpc::Status::OK;
    }

    grpc::Status GetBlobsInfos(
        grpc::ServerContext* context, const smproto::BlobsInfosRequest* request, smproto::BlobsInfos* response) override
    {
        (void)context;

        for (const auto& digest : request->digests()) {
            response->add_urls("http://example.com/blobs/" + digest);
        }

        return grpc::Status::OK;
    }

    grpc::Status GetNodeNetworkParams(grpc::ServerContext*, const smproto::GetNodeNetworkParamsRequest* request,
        smproto::GetNodeNetworkParamsResponse* response) override
    {
        return OnGetNodeNetworkParams(request, response);
    }

    grpc::Status AllocateInstanceNetwork(grpc::ServerContext*, const smproto::AllocateInstanceNetworkRequest* request,
        smproto::AllocateInstanceNetworkResponse* response) override
    {
        return OnAllocateInstanceNetwork(request, response);
    }

    grpc::Status ReleaseInstanceNetwork(grpc::ServerContext*, const smproto::ReleaseInstanceNetworkRequest* request,
        smproto::ReleaseInstanceNetworkResponse* response) override
    {
        return OnReleaseInstanceNetwork(request, response);
    }

    grpc::Status ReleaseNodeNetwork(grpc::ServerContext*, const smproto::ReleaseNodeNetworkRequest* request,
        smproto::ReleaseNodeNetworkResponse* response) override
    {
        return OnReleaseNodeNetwork(request, response);
    }

    grpc::Status SyncNetworkState(grpc::ServerContext*, const smproto::SyncNetworkStateRequest* request,
        smproto::SyncNetworkStateResponse* response) override
    {
        return OnSyncNetworkState(request, response);
    }

    grpc::Status SubscribeInstanceNetworkUpdates(grpc::ServerContext* context,
        const smproto::SubscribeInstanceNetworkUpdatesRequest*,
        grpc::ServerWriter<smproto::InstanceNetworkUpdateNotification>* writer) override
    {
        {
            std::lock_guard lock {mLock};
            mNetworkUpdateWriter     = writer;
            mNetworkUpdateContext    = context;
            mNetworkUpdateSubscribed = true;
            mCV.notify_all();
        }

        std::unique_lock lock {mLock};
        while (!context->IsCancelled()) {
            mCV.wait_for(lock, std::chrono::milliseconds(100));
        }

        mNetworkUpdateWriter = nullptr;

        return grpc::Status::OK;
    }

    void SendPendingFirewallUpdate(const smproto::PendingFirewallUpdate& update)
    {
        std::lock_guard lock {mLock};

        if (mNetworkUpdateWriter) {
            smproto::InstanceNetworkUpdateNotification notification;
            *notification.mutable_pending_firewall_update() = update;
            mNetworkUpdateWriter->Write(notification);
        }
    }

    bool WaitNetworkUpdateSubscribed(std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mLock};
        return mCV.wait_for(lock, timeout, [this]() { return mNetworkUpdateSubscribed; });
    }

    MOCK_METHOD(grpc::Status, OnGetNodeNetworkParams,
        (const smproto::GetNodeNetworkParamsRequest*, smproto::GetNodeNetworkParamsResponse*));
    MOCK_METHOD(grpc::Status, OnAllocateInstanceNetwork,
        (const smproto::AllocateInstanceNetworkRequest*, smproto::AllocateInstanceNetworkResponse*));
    MOCK_METHOD(grpc::Status, OnReleaseInstanceNetwork,
        (const smproto::ReleaseInstanceNetworkRequest*, smproto::ReleaseInstanceNetworkResponse*));
    MOCK_METHOD(grpc::Status, OnReleaseNodeNetwork,
        (const smproto::ReleaseNodeNetworkRequest*, smproto::ReleaseNodeNetworkResponse*));
    MOCK_METHOD(grpc::Status, OnSyncNetworkState,
        (const smproto::SyncNetworkStateRequest*, smproto::SyncNetworkStateResponse*));

    MOCK_METHOD(void, OnSMInfo, (const smproto::SMInfo&));
    MOCK_METHOD(void, OnNodeInstancesStatus, (const smproto::NodeInstancesStatus&));
    MOCK_METHOD(void, OnUpdateInstancesStatus, (const smproto::UpdateInstancesStatus&));
    MOCK_METHOD(void, OnInstantMonitoring, (const smproto::InstantMonitoring&));
    MOCK_METHOD(void, OnAlert, (const smproto::Alert&));
    MOCK_METHOD(void, OnNodeConfigStatus, (const smproto::NodeConfigStatus&));
    MOCK_METHOD(void, OnAverageMonitoring, (const smproto::AverageMonitoring&));
    MOCK_METHOD(void, OnLogData, (const smproto::LogData&));

    void SendGetNodeConfigStatus()
    {
        smproto::SMIncomingMessages msg;
        msg.mutable_get_node_config_status();
        mStream->Write(msg);
    }

    void SendCheckNodeConfig(const std::string& nodeConfig, const std::string& version)
    {
        smproto::SMIncomingMessages msg;
        auto*                       checkConfig = msg.mutable_check_node_config();
        checkConfig->set_node_config(nodeConfig);
        checkConfig->set_version(version);
        mStream->Write(msg);
    }

    void SendSetNodeConfig(const std::string& nodeConfig, const std::string& version)
    {
        smproto::SMIncomingMessages msg;
        auto*                       setConfig = msg.mutable_set_node_config();
        setConfig->set_node_config(nodeConfig);
        setConfig->set_version(version);
        mStream->Write(msg);
    }

    void SendUpdateInstances(
        const std::vector<smproto::InstanceInfo>& startInstances, const std::vector<std::string>& stopServiceIds)
    {
        smproto::SMIncomingMessages msg;
        auto*                       updateInstances = msg.mutable_update_instances();

        for (const auto& instance : startInstances) {
            *updateInstances->add_start_instances() = instance;
        }

        for (const auto& serviceId : stopServiceIds) {
            auto* ident = updateInstances->add_stop_instances();
            ident->set_item_id(serviceId);
        }

        mStream->Write(msg);
    }

    void SendSystemLogRequest(const std::string& correlationId)
    {
        smproto::SMIncomingMessages msg;
        auto*                       request = msg.mutable_system_log_request();
        request->set_correlation_id(correlationId);
        mStream->Write(msg);
    }

    void SendInstanceLogRequest(const std::string& correlationId, const std::string& serviceId)
    {
        smproto::SMIncomingMessages msg;
        auto*                       request = msg.mutable_instance_log_request();
        request->set_correlation_id(correlationId);
        request->mutable_filter()->set_item_id(serviceId);
        mStream->Write(msg);
    }

    void SendInstanceCrashLogRequest(const std::string& correlationId, const std::string& serviceId)
    {
        smproto::SMIncomingMessages msg;
        auto*                       request = msg.mutable_instance_crash_log_request();
        request->set_correlation_id(correlationId);
        request->mutable_filter()->set_item_id(serviceId);
        mStream->Write(msg);
    }

    void SendGetAverageMonitoring()
    {
        smproto::SMIncomingMessages msg;
        msg.mutable_get_average_monitoring();
        mStream->Write(msg);
    }

    void SendConnectionStatus(smproto::ConnectionEnum status)
    {
        smproto::SMIncomingMessages msg;
        msg.mutable_connection_status()->set_cloud_status(status);
        mStream->Write(msg);
    }

    void WaitRegistered(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mRegistered; });
        mRegistered = false;
    }

    void WaitSMInfo(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mSMInfoReceived; });
        mSMInfoReceived = false;
    }

    void WaitNodeInstancesStatus(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mNodeInstancesStatusReceived; });
        mNodeInstancesStatusReceived = false;
    }

    void WaitUpdateInstancesStatus(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mUpdateInstancesStatusReceived; });
        mUpdateInstancesStatusReceived = false;
    }

    void WaitInstantMonitoring(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mInstantMonitoringReceived; });
        mInstantMonitoringReceived = false;
    }

    void WaitAlert(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mAlertReceived; });
        mAlertReceived = false;
    }

    void WaitNodeConfigStatus(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mNodeConfigStatusReceived; });
        mNodeConfigStatusReceived = false;
    }

    void WaitAverageMonitoring(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mAverageMonitoringReceived; });
        mAverageMonitoringReceived = false;
    }

    void WaitLogData(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout, [this] { return mLogDataReceived; });
        mLogDataReceived = false;
    }

private:
    std::unique_ptr<grpc::Server> CreateServer(
        const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials)
    {
        grpc::ServerBuilder builder;

        aos::common::utils::SetGRPCServerOptions(builder);

        builder.AddListeningPort(addr, credentials);
        builder.RegisterService(static_cast<smproto::SMService::Service*>(this));
        builder.RegisterService(static_cast<smproto::NetworkService::Service*>(this));

        return builder.BuildAndStart();
    }

    grpc::ServerReaderWriter<smproto::SMIncomingMessages, smproto::SMOutgoingMessages>* mStream {};
    grpc::ServerContext*                                                                mCtx {};

    std::mutex              mLock;
    std::condition_variable mCV;

    bool                          mRegistered {};
    bool                          mSMInfoReceived {};
    bool                          mNodeInstancesStatusReceived {};
    bool                          mUpdateInstancesStatusReceived {};
    bool                          mInstantMonitoringReceived {};
    bool                          mAlertReceived {};
    bool                          mNodeConfigStatusReceived {};
    bool                          mAverageMonitoringReceived {};
    bool                          mLogDataReceived {};
    std::unique_ptr<grpc::Server> mServer;

    grpc::ServerWriter<smproto::InstanceNetworkUpdateNotification>* mNetworkUpdateWriter {};
    grpc::ServerContext*                                            mNetworkUpdateContext {};
    bool                                                            mNetworkUpdateSubscribed {};
};

#endif
