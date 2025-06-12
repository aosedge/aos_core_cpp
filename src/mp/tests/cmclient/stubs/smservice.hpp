/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SMSERVICE_HPP_
#define SMSERVICE_HPP_

#include <condition_variable>
#include <mutex>

#include <grpcpp/security/credentials.h>
#include <grpcpp/server_builder.h>

#include <servicemanager/v4/servicemanager.grpc.pb.h>

/**
 * TestSMService class.
 */
class TestSMService : public servicemanager::v4::SMService::Service {
public:
    /**
     * Constructor.
     *
     * @param url URL.
     */
    TestSMService(const std::string& url) { mServer = CreatePublicServer(url, grpc::InsecureServerCredentials()); }

    /**
     * Register SM.
     *
     * @param context Context.
     * @param stream Stream.
     * @return grpc::Status.
     */
    grpc::Status RegisterSM([[maybe_unused]] grpc::ServerContext* context,
        grpc::ServerReaderWriter<servicemanager::v4::SMIncomingMessages, servicemanager::v4::SMOutgoingMessages>*
            stream) override
    {
        try {
            servicemanager::v4::SMOutgoingMessages outgoingMsg;

            mStream    = stream;
            mConnected = true;
            mCV.notify_all();

            while (stream->Read(&outgoingMsg)) {
                mOutgoingMsg = outgoingMsg;
                mReceived.store(true);
                mCV.notify_all();
            }
        } catch (const std::exception& e) {
        }

        mConnected = false;
        mCV.notify_all();

        return grpc::Status::OK;
    }

    /**
     * Wait for response.
     */
    void WaitForResponse()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mReceived.load(); });

        mReceived.store(false);
    }

    /**
     * Wait for connection.
     *
     * @return True if connected.
     */
    bool WaitForConnection()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [this] { return mConnected; });

        return mConnected;
    }

    /**
     * Wait for disconnection.
     *
     * @return True if disconnected.
     */
    bool WaitForDisconnection()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [this] { return !mConnected; });

        return !mConnected;
    }

    /**
     * Get outgoing message.
     *
     * @return Outgoing message.
     */
    const servicemanager::v4::SMOutgoingMessages& GetOutgoingMsg() const { return mOutgoingMsg; }

    /**
     * Send GetNodeConfigStatus.
     *
     * @return bool.
     */
    bool SendGetNodeConfigStatus()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        incomingMsg.mutable_get_node_config_status();

        return mStream->Write(incomingMsg);
    }

    /**
     * Send CheckNodeConfig.
     *
     * @return bool.
     */
    bool SendCheckNodeConfig()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   checkUnitConfig = incomingMsg.mutable_check_node_config();
        checkUnitConfig->set_node_config("unit_config");
        checkUnitConfig->set_version("version");

        return mStream->Write(incomingMsg);
    }

    /**
     * Send SetUnitConfig.
     *
     * @return bool.
     */
    bool SendSetUnitConfig()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   setUnitConfig = incomingMsg.mutable_set_node_config();
        setUnitConfig->set_node_config("unit_config");
        setUnitConfig->set_version("version");

        return mStream->Write(incomingMsg);
    }

    /**
     * Send RunInstance.
     *
     * @return bool.
     */
    bool SendRunInstances()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   runInstances = incomingMsg.mutable_run_instances();
        auto                                   serviceInfo  = runInstances->add_services();
        serviceInfo->set_service_id("service_id");
        serviceInfo->set_provider_id("provider_id");
        serviceInfo->set_version("version");
        serviceInfo->set_gid(1);
        serviceInfo->set_url("url");
        serviceInfo->set_sha256("sha256");
        serviceInfo->set_size(1);

        auto layerInfo = runInstances->add_layers();
        layerInfo->set_layer_id("layer_id");
        layerInfo->set_digest("digest");
        layerInfo->set_version("version");
        layerInfo->set_url("url");
        layerInfo->set_sha256("sha256");
        layerInfo->set_size(1);

        auto instanceInfo  = runInstances->add_instances();
        auto instanceIdent = instanceInfo->mutable_instance();
        instanceIdent->set_service_id("service_id");
        instanceIdent->set_subject_id("subject_id");
        instanceIdent->set_instance(1);
        instanceInfo->set_uid(1);
        instanceInfo->set_priority(1);
        instanceInfo->set_storage_path("storage_path");
        instanceInfo->set_state_path("state_path");

        auto networkParameters = instanceInfo->mutable_network_parameters();
        networkParameters->set_network_id("network_id");
        networkParameters->set_subnet("subnet");
        networkParameters->set_ip("ip");
        networkParameters->set_vlan_id(1);
        networkParameters->add_dns_servers("dns_servers");
        auto firewallRule = networkParameters->add_rules();
        firewallRule->set_dst_ip("dst_ip");
        firewallRule->set_dst_port("dst_port");
        firewallRule->set_proto("proto");
        firewallRule->set_src_ip("src_ip");

        return mStream->Write(incomingMsg);
    }

    /**
     * Send OverrideEnvVars.
     *
     * @return bool.
     */
    bool SendOverrideEnvVars()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   overrideEnvVars        = incomingMsg.mutable_override_env_vars();
        auto                                   overrideInstanceEnvVar = overrideEnvVars->add_env_vars();
        auto                                   instanceFilter = overrideInstanceEnvVar->mutable_instance_filter();
        instanceFilter->set_service_id("service_id");
        instanceFilter->set_subject_id("subject_id");
        instanceFilter->set_instance(1);

        auto envVarInfo = overrideInstanceEnvVar->add_variables();
        envVarInfo->set_name("name");
        envVarInfo->set_value("value");

        return mStream->Write(incomingMsg);
    }

    /**
     * Send SystemLogRequest.
     *
     * @return bool.
     */
    bool SendSystemLogRequest()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   systemLogRequest = incomingMsg.mutable_system_log_request();
        systemLogRequest->set_log_id("log_id");
        auto from = systemLogRequest->mutable_from();
        from->set_seconds(1);
        from->set_nanos(1);
        auto till = systemLogRequest->mutable_till();
        till->set_seconds(1);
        till->set_nanos(1);

        return mStream->Write(incomingMsg);
    }

    /**
     * Send InstanceLogRequest.
     *
     * @return bool.
     */
    bool SendInstanceLogRequest()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   instanceLogRequest = incomingMsg.mutable_instance_log_request();
        instanceLogRequest->set_log_id("log_id");
        auto instanceFilter = instanceLogRequest->mutable_instance_filter();
        instanceFilter->set_service_id("service_id");
        instanceFilter->set_subject_id("subject_id");
        instanceFilter->set_instance(1);
        auto from = instanceLogRequest->mutable_from();
        from->set_seconds(1);
        from->set_nanos(1);
        auto till = instanceLogRequest->mutable_till();
        till->set_seconds(1);
        till->set_nanos(1);

        return mStream->Write(incomingMsg);
    }

    /**
     * Send InstanceCrashLogRequest.
     *
     * @return bool.
     */
    bool SendInstanceCrashLogRequest()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto instanceCrashLogRequest = incomingMsg.mutable_instance_crash_log_request();
        instanceCrashLogRequest->set_log_id("log_id");
        auto instanceFilter = instanceCrashLogRequest->mutable_instance_filter();
        instanceFilter->set_service_id("service_id");
        instanceFilter->set_subject_id("subject_id");
        instanceFilter->set_instance(1);
        auto from = instanceCrashLogRequest->mutable_from();
        from->set_seconds(1);
        from->set_nanos(1);
        auto till = instanceCrashLogRequest->mutable_till();
        till->set_seconds(1);
        till->set_nanos(1);

        return mStream->Write(incomingMsg);
    }

    /**
     * Send GetAverageMonitoring.
     *
     * @return bool.
     */
    bool SendGetAverageMonitoring()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        incomingMsg.mutable_get_average_monitoring();

        return mStream->Write(incomingMsg);
    }

    /**
     * Send ConnectionStatus.
     *
     * @return bool.
     */
    bool SendConnectionStatus()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   connectionStatus = incomingMsg.mutable_connection_status();
        connectionStatus->set_cloud_status(servicemanager::v4::ConnectionEnum::CONNECTED);

        return mStream->Write(incomingMsg);
    }

    /**
     * Send ImageContentInfo.
     *
     * @return bool.
     */
    bool SendImageContentInfo()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   imageContentInfo = incomingMsg.mutable_image_content_info();
        imageContentInfo->set_request_id(1);

        auto imageFile = imageContentInfo->add_image_files();
        imageFile->set_relative_path("relative_path");
        imageFile->set_sha256("sha256");
        imageFile->set_size(1);

        auto errorInfo = imageContentInfo->mutable_error();
        errorInfo->set_aos_code(1);
        errorInfo->set_exit_code(1);
        errorInfo->set_message("message");

        return mStream->Write(incomingMsg);
    }

    /**
     * Send ImageContent.
     *
     * @return bool.
     */
    bool SendImageContent()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   imageContent = incomingMsg.mutable_image_content();
        imageContent->set_request_id(1);
        imageContent->set_relative_path("relative_path");
        imageContent->set_parts_count(1);
        imageContent->set_part(1);
        imageContent->set_data("data");

        return mStream->Write(incomingMsg);
    }

    /**
     * Send UpdateNetworks.
     *
     * @return bool.
     */
    bool SendUpdateNetworks()
    {
        std::unique_lock lock {mMutex};

        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   updateNetworks    = incomingMsg.mutable_update_networks();
        auto                                   networkParameters = updateNetworks->add_networks();
        networkParameters->set_network_id("network_id");
        networkParameters->set_subnet("subnet");
        networkParameters->set_ip("ip");
        networkParameters->set_vlan_id(1);
        networkParameters->add_dns_servers("dns_servers");
        auto firewallRule = networkParameters->add_rules();
        firewallRule->set_dst_ip("dst_ip");
        firewallRule->set_dst_port("dst_port");
        firewallRule->set_proto("proto");
        firewallRule->set_src_ip("src_ip");

        return mStream->Write(incomingMsg);
    }

    /**
     * Send ClockSync.
     *
     * @return bool.
     */
    bool SendClockSync()
    {
        std::unique_lock lock {mMutex};
        mCV.wait_for(lock, cWaitTimeout, [&] { return mStream != nullptr; });

        servicemanager::v4::SMIncomingMessages incomingMsg;
        auto                                   clockSync   = incomingMsg.mutable_clock_sync();
        auto                                   currentTime = clockSync->mutable_current_time();
        currentTime->set_seconds(1);
        currentTime->set_nanos(1);

        return mStream->Write(incomingMsg);
    }

private:
    constexpr static auto cWaitTimeout = std::chrono::seconds(10);

    grpc::ServerReaderWriter<servicemanager::v4::SMIncomingMessages, servicemanager::v4::SMOutgoingMessages>*
        mStream {};

    std::unique_ptr<grpc::Server> CreatePublicServer(
        const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials)
    {
        grpc::ServerBuilder builder;

        builder.AddListeningPort(addr, credentials);
        builder.RegisterService(static_cast<servicemanager::v4::SMService::Service*>(this));

        return builder.BuildAndStart();
    }

    std::unique_ptr<grpc::Server>          mServer;
    std::mutex                             mMutex;
    std::condition_variable                mCV;
    servicemanager::v4::SMOutgoingMessages mOutgoingMsg;
    std::atomic<bool>                      mReceived {false};
    bool                                   mConnected {};
};

#endif /* SMSERVICE_HPP_ */
