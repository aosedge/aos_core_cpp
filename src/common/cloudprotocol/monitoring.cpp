/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <core/common/cloudprotocol/protocol.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "monitoring.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr PartitionUsageToJSON(const aos::cloudprotocol::PartitionUsage& usage)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("name", usage.mName.CStr());
    json->set("usedSize", usage.mUsedSize);

    return json;
}

void PartitionUsageFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::PartitionUsage& usage)
{
    auto err = usage.mName.Assign(json.GetValue<std::string>("name", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse partition name");

    usage.mUsedSize = json.GetValue<size_t>("usedSize", 0);
}

Poco::JSON::Object::Ptr MonitoringDataToJSON(const aos::cloudprotocol::MonitoringData& data)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto utcTime = data.mTime.ToUTCString();
    AOS_ERROR_CHECK_AND_THROW(utcTime.mError, "failed to convert time to UTC string");

    json->set("timestamp", utcTime.mValue.CStr());
    json->set("ram", data.mRAM);
    json->set("cpu", data.mCPU);
    json->set("download", data.mDownload);
    json->set("upload", data.mUpload);

    if (!data.mPartitions.IsEmpty()) {
        json->set("partitions", utils::ToJsonArray(data.mPartitions, PartitionUsageToJSON));
    }

    return json;
}

void MonitoringDataFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::MonitoringData& data)
{
    Error err = ErrorEnum::eNone;

    Tie(data.mTime, err) = common::utils::FromUTCString(json.GetValue<std::string>("timestamp", ""));
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse timestamp");

    data.mRAM      = json.GetValue<size_t>("ram", 0);
    data.mCPU      = json.GetValue<size_t>("cpu", 0);
    data.mDownload = json.GetValue<size_t>("download", 0);
    data.mUpload   = json.GetValue<size_t>("upload", 0);

    utils::ForEach(json, "partitions", [&data](const auto& partitionJson) {
        auto err = data.mPartitions.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add partition usage info");

        PartitionUsageFromJSON(utils::CaseInsensitiveObjectWrapper(partitionJson), data.mPartitions.Back());
    });
}

Poco::JSON::Object::Ptr NodeMonitoringDataToJSON(const aos::cloudprotocol::NodeMonitoringData& node)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("nodeId", node.mNodeID.CStr());
    json->set("items", utils::ToJsonArray(node.mItems, MonitoringDataToJSON));

    return json;
}

void NodeMonitoringDataFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::NodeMonitoringData& node)
{
    auto err = node.mNodeID.Assign(json.GetValue<std::string>("nodeId", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse node ID");

    utils::ForEach(json, "items", [&node](const auto& itemJson) {
        auto err = node.mItems.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add monitoring data item");

        MonitoringDataFromJSON(utils::CaseInsensitiveObjectWrapper(itemJson), node.mItems.Back());
    });
}

Poco::JSON::Object::Ptr InstanceMonitoringDataToJSON(const aos::cloudprotocol::InstanceMonitoringData& instance)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(instance.mInstanceIdent, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instance ident to JSON");

    json->set("nodeId", instance.mNodeID.CStr());
    json->set("items", utils::ToJsonArray(instance.mItems, MonitoringDataToJSON));

    return json;
}

void InstanceMonitoringDataFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::InstanceMonitoringData& instance)
{
    auto err = FromJSON(json, instance.mInstanceIdent);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse instance ident from JSON");

    err = instance.mNodeID.Assign(json.GetValue<std::string>("nodeId", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse nodeId");

    utils::ForEach(json, "items", [&instance](const auto& itemJson) {
        auto err = instance.mItems.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add monitoring data item");

        MonitoringDataFromJSON(utils::CaseInsensitiveObjectWrapper(itemJson), instance.mItems.Back());
    });
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::Monitoring& monitoring)
{
    try {
        if (!json.Has("nodes")) {
            return Error(ErrorEnum::eInvalidArgument, "nodes tag is required");
        }

        utils::ForEach(json, "nodes", [&monitoring](const auto& nodeJson) {
            auto err = monitoring.mNodes.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to add node monitoring data");

            NodeMonitoringDataFromJSON(utils::CaseInsensitiveObjectWrapper(nodeJson), monitoring.mNodes.Back());
        });

        utils::ForEach(json, "serviceInstances", [&monitoring](const auto& instanceJson) {
            auto err = monitoring.mServiceInstances.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to add service instance monitoring data");

            InstanceMonitoringDataFromJSON(
                utils::CaseInsensitiveObjectWrapper(instanceJson), monitoring.mServiceInstances.Back());
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::Monitoring& monitoring, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType(aos::cloudprotocol::MessageTypeEnum::eMonitoringData);

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("nodes", utils::ToJsonArray(monitoring.mNodes, NodeMonitoringDataToJSON));

        if (!monitoring.mServiceInstances.IsEmpty()) {
            json.set(
                "serviceInstances", utils::ToJsonArray(monitoring.mServiceInstances, InstanceMonitoringDataToJSON));
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
