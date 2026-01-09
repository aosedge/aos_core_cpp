/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>

#include <core/common/tools/logger.hpp>

#include <common/pbconvert/sm.hpp>
#include <common/utils/exception.hpp>

#include "smhandler.hpp"

namespace aos::cm::smcontroller {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

SMHandler::SMHandler(grpc::ServerContext*                                                                     context,
    grpc::ServerReaderWriter<servicemanager::v5::SMIncomingMessages, servicemanager::v5::SMOutgoingMessages>* stream,
    alerts::ReceiverItf& alertsReceiver, SenderItf& logSender, launcher::SenderItf& envVarsStatusSender,
    monitoring::ReceiverItf& monitoringReceiver, launcher::InstanceStatusReceiverItf& instanceStatusReceiver,
    nodeinfoprovider::SMInfoReceiverItf& smInfoReceiver, NodeConnectionStatusListenerItf& connStatusListener)
    : mContext(context)
    , mStream(stream)
    , mAlertsReceiver(&alertsReceiver)
    , mLogSender(&logSender)
    , mEnvVarsStatusSender(&envVarsStatusSender)
    , mMonitoringReceiver(&monitoringReceiver)
    , mInstanceStatusReceiver(&instanceStatusReceiver)
    , mSMInfoReceiver(&smInfoReceiver)
    , mConnStatusListener(&connStatusListener)
{
}

void SMHandler::Start()
{
    LOG_INF() << "Start SM handler";

    mStopProcessing.store(false);

    mSyncMessageSender.Init(mStream, cResponseTime);
    mSyncMessageSender.RegisterResponseHandler(
        [](const servicemanager::v5::SMOutgoingMessages& msg) { return msg.has_node_config_status(); },
        [](const servicemanager::v5::SMOutgoingMessages& src, servicemanager::v5::SMOutgoingMessages& dst) {
            dst.mutable_node_config_status()->CopyFrom(src.node_config_status());
        });
    mSyncMessageSender.RegisterResponseHandler(
        [](const servicemanager::v5::SMOutgoingMessages& msg) { return msg.has_average_monitoring(); },
        [](const servicemanager::v5::SMOutgoingMessages& src, servicemanager::v5::SMOutgoingMessages& dst) {
            dst.mutable_average_monitoring()->CopyFrom(src.average_monitoring());
        });

    mProcessThread = std::thread([this]() { ProcessMessages(); });
}

void SMHandler::Wait()
{
    if (mProcessThread.joinable()) {
        mProcessThread.join();
    }
}

void SMHandler::Stop()
{
    LOG_INF() << "Stop SM handler";

    mStopProcessing.store(true);

    if (mContext) {
        mContext->TryCancel();
    }
}

String SMHandler::GetNodeID() const
{
    return mNodeID;
}

Error SMHandler::GetNodeConfigStatus(NodeConfigStatus& status)
{
    LOG_DBG() << "Get node configuration status" << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    servicemanager::v5::SMOutgoingMessages outMsg;

    // Create a get_node_config_status request in the incoming message
    inMsg.mutable_get_node_config_status();

    auto* nodeConfigStatus = outMsg.mutable_node_config_status();

    if (auto err = mSyncMessageSender.SendSync(inMsg, outMsg); !err.IsNone()) {
        return err;
    }

    if (auto err = common::pbconvert::ConvertFromProto(*nodeConfigStatus, status); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error SMHandler::CheckNodeConfig(const NodeConfig& config)
{
    LOG_DBG() << "Check node config for node" << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    servicemanager::v5::SMOutgoingMessages outMsg;

    auto* checkNodeConfig = inMsg.mutable_check_node_config();
    if (auto err = common::pbconvert::ConvertToProto(config, *checkNodeConfig); !err.IsNone()) {
        return err;
    }

    auto* nodeConfigStatus = outMsg.mutable_node_config_status();

    if (auto err = mSyncMessageSender.SendSync(inMsg, outMsg); !err.IsNone()) {
        return err;
    }

    return common::pbconvert::ConvertFromProto(nodeConfigStatus->error());
}

Error SMHandler::UpdateNodeConfig(const NodeConfig& config)
{
    LOG_DBG() << "Update node config for node" << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    servicemanager::v5::SMOutgoingMessages outMsg;

    auto* setNodeConfig = inMsg.mutable_set_node_config();
    if (auto err = common::pbconvert::ConvertToProto(config, *setNodeConfig); !err.IsNone()) {
        return err;
    }

    auto* nodeConfigStatus = outMsg.mutable_node_config_status();

    if (auto err = mSyncMessageSender.SendSync(inMsg, outMsg); !err.IsNone()) {
        return err;
    }

    return common::pbconvert::ConvertFromProto(nodeConfigStatus->error());
}

Error SMHandler::RequestLog(const aos::RequestLog& log)
{
    LOG_DBG() << "Request log" << Log::Field("correlationId", log.mCorrelationID) << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    servicemanager::v5::SMOutgoingMessages outMsg;

    switch (log.mLogType.GetValue()) {
    case LogTypeEnum::eSystemLog: {
        auto* systemLogRequest = inMsg.mutable_system_log_request();
        if (auto err = common::pbconvert::ConvertToProto(log, *systemLogRequest); !err.IsNone()) {
            return err;
        }

        break;
    }

    case LogTypeEnum::eInstanceLog: {
        auto* instanceLogRequest = inMsg.mutable_instance_log_request();
        if (auto err = common::pbconvert::ConvertToProto(log, *instanceLogRequest); !err.IsNone()) {
            return err;
        }

        break;
    }

    case LogTypeEnum::eCrashLog: {
        auto* instanceCrashLogRequest = inMsg.mutable_instance_crash_log_request();
        if (auto err = common::pbconvert::ConvertToProto(log, *instanceCrashLogRequest); !err.IsNone()) {
            return err;
        }

        break;
    }

    default:
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotSupported, "unknown log type"));
    }

    if (auto err = SendMessage(inMsg); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error SMHandler::UpdateNetworks(const Array<UpdateNetworkParameters>& networkParameters)
{
    LOG_DBG() << "Update networks for node" << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    auto*                                  updateNetworks = inMsg.mutable_update_networks();

    if (auto err = common::pbconvert::ConvertToProto(networkParameters, *updateNetworks); !err.IsNone()) {
        return err;
    }

    if (auto err = SendMessage(inMsg); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error SMHandler::ProcessMessages()
{
    try {
        servicemanager::v5::SMOutgoingMessages outgoingMsg;

        while (!mStopProcessing.load() && mStream->Read(&outgoingMsg)) {
            Error err;

            if (outgoingMsg.has_sm_info()) {
                err = ProcessSMInfo(outgoingMsg.sm_info());
            } else if (outgoingMsg.has_update_instances_status()) {
                err = ProcessUpdateInstancesStatus(outgoingMsg.update_instances_status());
            } else if (outgoingMsg.has_node_instances_status()) {
                err = ProcessNodeInstancesStatus(outgoingMsg.node_instances_status());
            } else if (outgoingMsg.has_log()) {
                err = ProcessLogData(outgoingMsg.log());
            } else if (outgoingMsg.has_instant_monitoring()) {
                err = ProcessInstantMonitoring(outgoingMsg.instant_monitoring());
            } else if (outgoingMsg.has_alert()) {
                err = ProcessAlert(outgoingMsg.alert());
            } else if (auto processErr = mSyncMessageSender.ProcessResponse(outgoingMsg); processErr.HasValue()) {
                err = processErr.GetValue();
            } else {
                LOG_WRN() << "Unknown message type received";
            }

            if (!err.IsNone()) {
                LOG_ERR() << "Failed to process message" << Log::Field("nodeID", GetNodeID()) << Log::Field(err);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Handle incoming messages failed" << Log::Field(AOS_ERROR_WRAP(common::utils::ToAosError(e)));
    }

    mConnStatusListener->OnNodeDisconnected(GetNodeID());

    return ErrorEnum::eNone;
}

Error SMHandler::SendMessage(const servicemanager::v5::SMIncomingMessages& message)
{
    std::lock_guard lock {mMutex};

    if (!mStream->Write(message)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to send message"));
    }

    return ErrorEnum::eNone;
}

Error SMHandler::ProcessSMInfo(const servicemanager::v5::SMInfo& smInfo)
{
    LOG_DBG() << "Process SM info" << Log::Field("nodeID", smInfo.node_id().c_str());

    auto aosSMInfo = std::make_unique<aos::cm::nodeinfoprovider::SMInfo>();

    if (auto err = common::pbconvert::ConvertFromProto(smInfo, *aosSMInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (mNodeID.IsEmpty()) {
        mNodeID = aosSMInfo->mNodeID;

        mConnStatusListener->OnNodeConnected(GetNodeID());
    }

    if (auto err = mSMInfoReceiver->OnSMInfoReceived(*aosSMInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMHandler::ProcessUpdateInstancesStatus(const servicemanager::v5::UpdateInstancesStatus& status)
{
    LOG_DBG() << "Process update instances status" << Log::Field("nodeID", GetNodeID());

    for (const auto& grpcInstanceStatus : status.instances()) {
        auto instanceStatus = std::make_unique<InstanceStatus>();

        if (auto err = common::pbconvert::ConvertFromProto(grpcInstanceStatus, GetNodeID(), *instanceStatus);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = mInstanceStatusReceiver->OnInstanceStatusReceived(*instanceStatus); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error SMHandler::ProcessNodeInstancesStatus(const servicemanager::v5::NodeInstancesStatus& status)
{
    LOG_DBG() << "Process node instances status" << Log::Field("nodeID", GetNodeID());

    auto statuses = std::make_unique<StaticArray<InstanceStatus, cMaxNumInstances>>();

    for (const auto& grpcInstanceStatus : status.instances()) {
        if (auto err = statuses->EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = common::pbconvert::ConvertFromProto(grpcInstanceStatus, GetNodeID(), statuses->Back());
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (auto err = mInstanceStatusReceiver->OnNodeInstancesStatusesReceived(GetNodeID(), *statuses); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMHandler::ProcessLogData(const servicemanager::v5::LogData& logData)
{
    LOG_DBG() << "Process log data" << Log::Field("nodeID", GetNodeID())
              << Log::Field("correlationId", logData.correlation_id().c_str()) << Log::Field("part", logData.part())
              << Log::Field("partCount", logData.part_count());

    auto pushLog = std::make_unique<PushLog>();

    if (auto err = common::pbconvert::ConvertFromProto(logData, GetNodeID(), *pushLog); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mLogSender->SendLog(*pushLog); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMHandler::ProcessInstantMonitoring(const servicemanager::v5::InstantMonitoring& monitoring)
{
    LOG_DBG() << "Process instant monitoring" << Log::Field("nodeID", GetNodeID());

    auto nodeMonitoringData = std::make_unique<aos::monitoring::NodeMonitoringData>();

    if (auto err = common::pbconvert::ConvertFromProto(monitoring, GetNodeID(), *nodeMonitoringData); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mMonitoringReceiver->OnMonitoringReceived(*nodeMonitoringData); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMHandler::ProcessAlert(const servicemanager::v5::Alert& alert)
{
    LOG_DBG() << "Process alert" << Log::Field("nodeID", GetNodeID());

    if (mAlertsReceiver) {
        auto aosAlert = std::make_unique<AlertVariant>();

        if (auto err = common::pbconvert::ConvertFromProto(alert, GetNodeID(), *aosAlert); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mAlertsReceiver->OnAlertReceived(*aosAlert);
    }

    return ErrorEnum::eNone;
}

Error SMHandler::UpdateInstances(
    const Array<aos::InstanceInfo>& stopInstances, const Array<aos::InstanceInfo>& startInstances)
{
    LOG_DBG() << "Update instances for node" << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    auto*                                  updateInstances = inMsg.mutable_update_instances();

    if (auto err = common::pbconvert::ConvertToProto(stopInstances, startInstances, *updateInstances); !err.IsNone()) {
        return err;
    }

    if (auto err = SendMessage(inMsg); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error SMHandler::GetAverageMonitoring(aos::monitoring::NodeMonitoringData& monitoring)
{
    LOG_DBG() << "Get average monitoring data for node" << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    servicemanager::v5::SMOutgoingMessages outMsg;

    inMsg.mutable_get_average_monitoring();
    auto* averageMonitoring = outMsg.mutable_average_monitoring();

    if (auto err = mSyncMessageSender.SendSync(inMsg, outMsg); !err.IsNone()) {
        return err;
    }

    if (auto err = common::pbconvert::ConvertFromProto(*averageMonitoring, GetNodeID(), monitoring); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

void SMHandler::OnConnect()
{
    LOG_DBG() << "Node connected" << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    auto*                                  connectionStatus = inMsg.mutable_connection_status();
    connectionStatus->set_cloud_status(servicemanager::v5::CONNECTED);

    if (auto err = SendMessage(inMsg); !err.IsNone()) {
        LOG_ERR() << "Failed to send connection status" << Log::Field(err);
    }
}

void SMHandler::OnDisconnect()
{
    LOG_DBG() << "Node disconnected" << Log::Field("nodeID", GetNodeID());

    servicemanager::v5::SMIncomingMessages inMsg;
    auto*                                  connectionStatus = inMsg.mutable_connection_status();
    connectionStatus->set_cloud_status(servicemanager::v5::DISCONNECTED);

    if (auto err = SendMessage(inMsg); !err.IsNone()) {
        LOG_ERR() << "Failed to send connection status" << Log::Field(err);
    }
}

} // namespace aos::cm::smcontroller
