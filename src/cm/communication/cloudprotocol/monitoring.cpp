/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "monitoring.hpp"

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr PartitionUsageToJSON(const PartitionUsage& usage)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("name", usage.mName.CStr());
    json->set("usedSize", usage.mUsedSize);

    return json;
}

Poco::JSON::Object::Ptr MonitoringDataToJSON(const MonitoringData& data)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto utcTime = data.mTimestamp.ToUTCString();
    AOS_ERROR_CHECK_AND_THROW(utcTime.mError, "can't convert time to UTC string");

    json->set("timestamp", utcTime.mValue.CStr());
    json->set("ram", data.mRAM);
    json->set("cpu", data.mCPU);
    json->set("download", data.mDownload);
    json->set("upload", data.mUpload);

    if (!data.mPartitions.IsEmpty()) {
        json->set("partitions", common::utils::ToJsonArray(data.mPartitions, PartitionUsageToJSON));
    }

    return json;
}

Poco::JSON::Object::Ptr NodeStateInfoToJSON(const NodeStateInfo& state)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto utcTime = state.mTimestamp.ToUTCString();
    AOS_ERROR_CHECK_AND_THROW(utcTime.mError, "can't convert time to UTC string");

    json->set("timestamp", utcTime.mValue.CStr());
    json->set("provisioned", state.mProvisioned);
    json->set("state", state.mState.ToString().CStr());

    return json;
}

Poco::JSON::Object::Ptr NodeMonitoringDataToJSON(const NodeMonitoringData& node)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("node", CreateAosIdentity({node.mNodeID}));

    if (!node.mStates.IsEmpty()) {
        json->set("nodeStates", common::utils::ToJsonArray(node.mStates, NodeStateInfoToJSON));
    }

    json->set("items", common::utils::ToJsonArray(node.mItems, MonitoringDataToJSON));

    return json;
}

Poco::JSON::Object::Ptr InstanceStateInfoToJSON(const InstanceStateInfo& state)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto utcTime = state.mTimestamp.ToUTCString();
    AOS_ERROR_CHECK_AND_THROW(utcTime.mError, "can't convert time to UTC string");

    json->set("timestamp", utcTime.mValue.CStr());
    json->set("state", state.mState.ToString().CStr());

    return json;
}

Poco::JSON::Object::Ptr InstanceMonitoringDataToJSON(const InstanceMonitoringData& instance)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(static_cast<const InstanceIdent&>(instance), *json);
    AOS_ERROR_CHECK_AND_THROW(err, "can't convert instance ident to JSON");

    json->set("node", CreateAosIdentity({instance.mNodeID}));
    json->set("itemStates", common::utils::ToJsonArray(instance.mStates, InstanceStateInfoToJSON));
    json->set("items", common::utils::ToJsonArray(instance.mItems, MonitoringDataToJSON));

    return json;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ToJSON(const Monitoring& monitoring, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType(MessageTypeEnum::eMonitoringData);

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("nodes", common::utils::ToJsonArray(monitoring.mNodes, NodeMonitoringDataToJSON));

        if (!monitoring.mInstances.IsEmpty()) {
            json.set("instances", common::utils::ToJsonArray(monitoring.mInstances, InstanceMonitoringDataToJSON));
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
