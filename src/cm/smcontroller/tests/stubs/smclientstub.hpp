/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_SMCLIENTSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_SMCLIENTSTUB_HPP_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <core/common/tools/error.hpp>
#include <core/common/tools/optional.hpp>
#include <core/common/types/common.hpp>

#include <servicemanager/v5/servicemanager.grpc.pb.h>

namespace aos::cm::smcontroller {

/**
 * SM client stub for testing purposes.
 */
class SMClientStub {
public:
    SMClientStub()
        : mRunning {false}
    {
    }

    ~SMClientStub() { Stop(); }

    Error Init(const std::string& nodeID)
    {
        mNodeID = nodeID;

        return ErrorEnum::eNone;
    }

    Error Start(const std::string& url)
    {
        std::lock_guard lock {mMutex};

        if (mRunning) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "already running"));
        }

        mStub = CreateStub(url);
        if (!mStub) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to create stub"));
        }

        mContext = std::make_unique<grpc::ClientContext>();

        mStream = mStub->RegisterSM(mContext.get());
        if (!mStream) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to register SM"));
        }

        mRunning    = true;
        mReadThread = std::thread {[this]() { ReadLoop(); }};

        return ErrorEnum::eNone;
    }

    Error Stop()
    {
        {
            std::lock_guard lock {mMutex};

            if (!mRunning) {
                return ErrorEnum::eNone;
            }

            mRunning = false;

            if (mContext) {
                mContext->TryCancel();
            }

            if (mStream) {
                mStream->WritesDone();
                mStream->Finish();
            }
        }

        if (mReadThread.joinable()) {
            mReadThread.join();
        }

        return ErrorEnum::eNone;
    }

    void SendOutgoingMessage(const servicemanager::v5::SMOutgoingMessages& msg)
    {
        std::lock_guard lock {mMutex};

        if (mStream) {
            mStream->Write(msg);
        }
    }

    Error WaitUpdateNetworks()
    {
        std::unique_lock lock {mMutex};

        bool received = mUpdateNetworksCV.wait_for(
            lock, std::chrono::seconds(1), [this]() { return mUpdateNetworks.networks_size() > 0; });

        if (!received) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait update networks timeout"));
        }

        return ErrorEnum::eNone;
    }

    servicemanager::v5::UpdateNetworks GetUpdateNetworks() const
    {
        std::lock_guard lock {mMutex};

        return mUpdateNetworks;
    }

    Error SendUpdateInstancesStatus(const InstanceIdent& instanceIdent, InstanceState state)
    {
        std::lock_guard lock {mMutex};

        if (!mStream) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stream not available"));
        }

        servicemanager::v5::SMOutgoingMessages outMsg;
        auto*                                  updateStatus   = outMsg.mutable_update_instances_status();
        auto*                                  instanceStatus = updateStatus->add_instances();

        instanceStatus->mutable_instance()->set_item_id(instanceIdent.mItemID.CStr());
        instanceStatus->mutable_instance()->set_subject_id(instanceIdent.mSubjectID.CStr());
        instanceStatus->mutable_instance()->set_instance(instanceIdent.mInstance);
        instanceStatus->set_state(state.ToString().CStr());

        if (!mStream->Write(outMsg)) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to write update instances status"));
        }

        return ErrorEnum::eNone;
    }

    Error SendInstantMonitoring(const InstanceIdent& instanceIdent)
    {
        std::lock_guard lock {mMutex};

        if (!mStream) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stream not available"));
        }

        servicemanager::v5::SMOutgoingMessages outMsg;
        auto*                                  monitoring = outMsg.mutable_instant_monitoring();

        monitoring->mutable_node_monitoring()->set_cpu(75);
        monitoring->mutable_node_monitoring()->set_ram(2048);

        auto* instanceMonitoring = monitoring->add_instances_monitoring();

        instanceMonitoring->mutable_instance()->set_item_id(instanceIdent.mItemID.CStr());
        instanceMonitoring->mutable_instance()->set_subject_id(instanceIdent.mSubjectID.CStr());
        instanceMonitoring->mutable_instance()->set_instance(instanceIdent.mInstance);
        instanceMonitoring->mutable_monitoring_data()->set_cpu(80);
        instanceMonitoring->mutable_monitoring_data()->set_ram(1536);

        if (!mStream->Write(outMsg)) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to write instant monitoring"));
        }

        return ErrorEnum::eNone;
    }

    Error WaitCloudConnection()
    {
        std::unique_lock lock {mMutex};

        bool received
            = mCloudConnectionCV.wait_for(lock, std::chrono::seconds(1), [this]() { return mCloudStatus.HasValue(); });

        if (!received) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait cloud connection timeout"));
        }

        return ErrorEnum::eNone;
    }

    bool IsCloudConnected() const
    {
        std::lock_guard lock {mMutex};

        return mCloudStatus.HasValue() && mCloudStatus.GetValue();
    }

    Error SendSystemAlert(const std::string& message)
    {
        std::lock_guard lock {mMutex};

        if (!mStream) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stream not available"));
        }

        servicemanager::v5::SMOutgoingMessages outMsg;

        auto* alert = outMsg.mutable_alert();
        alert->mutable_system_alert()->set_message(message);

        if (!mStream->Write(outMsg)) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to write system alert"));
        }

        return ErrorEnum::eNone;
    }

    Error GetBlobsInfos(const std::vector<std::string>& digests, servicemanager::v5::BlobsInfos& blobsInfos)
    {
        std::lock_guard lock {mMutex};

        if (!mStub) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stub not available"));
        }

        servicemanager::v5::BlobsInfosRequest request;
        for (const auto& digest : digests) {
            request.add_digests(digest);
        }

        grpc::ClientContext context;
        auto                status = mStub->GetBlobsInfos(&context, request, &blobsInfos);

        if (!status.ok()) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, status.error_message().c_str()));
        }

        return ErrorEnum::eNone;
    }

private:
    std::unique_ptr<servicemanager::v5::SMService::Stub> CreateStub(const std::string& url)
    {
        auto channelCreds = grpc::InsecureChannelCredentials();

        auto channel = grpc::CreateChannel(url, channelCreds);
        if (!channel) {
            return nullptr;
        }

        return servicemanager::v5::SMService::NewStub(channel);
    }

    Error SendSMInfo()
    {
        std::lock_guard lock {mMutex};

        servicemanager::v5::SMOutgoingMessages outMsg;

        auto* smInfo = outMsg.mutable_sm_info();
        smInfo->set_node_id(mNodeID);

        if (!mStream) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stream not available"));
        }

        if (!mStream->Write(outMsg)) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to write SM info"));
        }

        return ErrorEnum::eNone;
    }

    void ProcessCheckNodeConfig(const servicemanager::v5::CheckNodeConfig& checkConfig)
    {
        if (!mStream) {
            return;
        }

        servicemanager::v5::SMOutgoingMessages outMsg;
        auto*                                  status = outMsg.mutable_node_config_status();

        status->set_version(checkConfig.version());
        status->set_state("active");

        mStream->Write(outMsg);
    }

    void ProcessSetNodeConfig(const servicemanager::v5::SetNodeConfig& setConfig)
    {
        if (!mStream) {
            return;
        }

        (void)setConfig;

        servicemanager::v5::SMOutgoingMessages outMsg;
        auto*                                  status = outMsg.mutable_node_config_status();

        status->set_version(setConfig.version());
        status->set_state("active");

        mStream->Write(outMsg);
    }

    void ProcessGetNodeConfigStatus()
    {
        if (!mStream) {
            return;
        }

        servicemanager::v5::SMOutgoingMessages outMsg;
        auto*                                  status = outMsg.mutable_node_config_status();

        status->set_version("1.0.0");
        status->set_state("installed");

        mStream->Write(outMsg);
    }

    void ProcessSystemLogRequest(const servicemanager::v5::SystemLogRequest& request)
    {
        if (!mStream) {
            return;
        }

        for (uint64_t part = 0; part < 2; ++part) {
            servicemanager::v5::SMOutgoingMessages outMsg;
            auto*                                  pushLog = outMsg.mutable_log();

            pushLog->set_correlation_id(request.correlation_id());
            pushLog->set_part(part);
            pushLog->set_part_count(2);
            pushLog->set_data("log data part " + std::to_string(part));
            pushLog->set_status("ok");

            mStream->Write(outMsg);
        }
    }

    void ProcessInstanceLogRequest(const servicemanager::v5::InstanceLogRequest& request)
    {
        if (!mStream) {
            return;
        }

        for (uint64_t part = 0; part < 2; ++part) {
            servicemanager::v5::SMOutgoingMessages outMsg;
            auto*                                  pushLog = outMsg.mutable_log();

            pushLog->set_correlation_id(request.correlation_id());
            pushLog->set_part(part);
            pushLog->set_part_count(2);
            pushLog->set_data("log data part " + std::to_string(part));
            pushLog->set_status("ok");

            mStream->Write(outMsg);
        }
    }

    void ProcessInstanceCrashLogRequest(const servicemanager::v5::InstanceCrashLogRequest& request)
    {
        if (!mStream) {
            return;
        }

        for (uint64_t part = 0; part < 2; ++part) {
            servicemanager::v5::SMOutgoingMessages outMsg;
            auto*                                  pushLog = outMsg.mutable_log();

            pushLog->set_correlation_id(request.correlation_id());
            pushLog->set_part(part);
            pushLog->set_part_count(2);
            pushLog->set_data("log data part " + std::to_string(part));
            pushLog->set_status("ok");

            mStream->Write(outMsg);
        }
    }

    void ProcessUpdateInstances(const servicemanager::v5::UpdateInstances& updateInstances)
    {
        if (!mStream) {
            return;
        }

        servicemanager::v5::SMOutgoingMessages outMsg;
        auto*                                  nodeStatus = outMsg.mutable_node_instances_status();

        for (const auto& instance : updateInstances.start_instances()) {
            auto* instanceStatus = nodeStatus->add_instances();

            instanceStatus->mutable_instance()->set_item_id(instance.instance().item_id());
            instanceStatus->mutable_instance()->set_subject_id(instance.instance().subject_id());
            instanceStatus->mutable_instance()->set_instance(instance.instance().instance());
            instanceStatus->set_state("activating");
        }

        mStream->Write(outMsg);
    }

    void ProcessGetAverageMonitoring()
    {
        if (!mStream) {
            return;
        }

        servicemanager::v5::SMOutgoingMessages outMsg;
        auto*                                  monitoring = outMsg.mutable_average_monitoring();

        monitoring->mutable_node_monitoring()->set_cpu(50);
        monitoring->mutable_node_monitoring()->set_ram(1024);

        mStream->Write(outMsg);
    }

    void ProcessConnectionStatus(const servicemanager::v5::ConnectionStatus& connectionStatus)
    {
        mCloudStatus.SetValue(connectionStatus.cloud_status());
        mCloudConnectionCV.notify_one();
    }

    void ProcessUpdateNetworks(const servicemanager::v5::UpdateNetworks& updateNetworks)
    {
        mUpdateNetworks.CopyFrom(updateNetworks);
        mUpdateNetworksCV.notify_one();
    }

    void ReadLoop()
    {
        SendSMInfo();

        while (true) {
            servicemanager::v5::SMIncomingMessages msg;

            if (!mStream || !mStream->Read(&msg)) {
                break;
            }

            {
                std::lock_guard lock {mMutex};

                if (!mRunning) {
                    break;
                }

                if (msg.has_check_node_config()) {
                    ProcessCheckNodeConfig(msg.check_node_config());
                }

                if (msg.has_set_node_config()) {
                    ProcessSetNodeConfig(msg.set_node_config());
                }

                if (msg.has_get_node_config_status()) {
                    ProcessGetNodeConfigStatus();
                }

                if (msg.has_system_log_request()) {
                    ProcessSystemLogRequest(msg.system_log_request());
                }

                if (msg.has_instance_log_request()) {
                    ProcessInstanceLogRequest(msg.instance_log_request());
                }

                if (msg.has_instance_crash_log_request()) {
                    ProcessInstanceCrashLogRequest(msg.instance_crash_log_request());
                }

                if (msg.has_update_networks()) {
                    ProcessUpdateNetworks(msg.update_networks());
                }

                if (msg.has_update_instances()) {
                    ProcessUpdateInstances(msg.update_instances());
                }

                if (msg.has_get_average_monitoring()) {
                    ProcessGetAverageMonitoring();
                }

                if (msg.has_connection_status()) {
                    ProcessConnectionStatus(msg.connection_status());
                }
            }
        }
    }

    std::string                                          mNodeID;
    std::atomic<bool>                                    mRunning;
    std::thread                                          mReadThread;
    mutable std::mutex                                   mMutex;
    std::unique_ptr<servicemanager::v5::SMService::Stub> mStub;
    std::unique_ptr<grpc::ClientContext>                 mContext;
    std::unique_ptr<
        grpc::ClientReaderWriter<servicemanager::v5::SMOutgoingMessages, servicemanager::v5::SMIncomingMessages>>
        mStream;

    servicemanager::v5::UpdateNetworks mUpdateNetworks;
    std::condition_variable            mUpdateNetworksCV;

    Optional<servicemanager::v5::ConnectionEnum> mCloudStatus;
    std::condition_variable                      mCloudConnectionCV;
};

} // namespace aos::cm::smcontroller

#endif
