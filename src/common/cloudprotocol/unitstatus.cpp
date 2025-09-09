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
#include "unitstatus.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr UnitConfigStatusToJSON(const aos::cloudprotocol::UnitConfigStatus& unitConfigStatus)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("version", unitConfigStatus.mVersion.CStr());
    json->set("status", unitConfigStatus.mStatus.ToString().CStr());

    if (!unitConfigStatus.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(unitConfigStatus.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

void UnitConfigStatusFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::UnitConfigStatus& unitConfigStatus)
{
    auto err = unitConfigStatus.mVersion.Assign(json.GetValue<std::string>("version", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse version");

    err = unitConfigStatus.mStatus.FromString(json.GetValue<std::string>("status", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse status");

    if (json.Has("errorInfo")) {
        err = FromJSON(json.GetObject("errorInfo"), unitConfigStatus.mError);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse errorInfo");
    }
}

Poco::JSON::Object::Ptr CPUInfoToJSON(const CPUInfo& cpuInfo)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("modelName", cpuInfo.mModelName.CStr());
    json->set("totalNumCores", cpuInfo.mNumCores);
    json->set("totalNumThreads", cpuInfo.mNumThreads);
    json->set("arch", cpuInfo.mArch.CStr());

    if (cpuInfo.mArchFamily.HasValue()) {
        json->set("archFamily", cpuInfo.mArchFamily->CStr());
    }

    if (cpuInfo.mMaxDMIPS.HasValue()) {
        json->set("maxDmips", *cpuInfo.mMaxDMIPS);
    }

    return json;
}

void CPUInfoFromJSON(const utils::CaseInsensitiveObjectWrapper& json, CPUInfo& cpuInfo)
{
    auto err = cpuInfo.mModelName.Assign(json.GetValue<std::string>("modelName", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse modelName");

    cpuInfo.mNumCores   = json.GetValue<size_t>("totalNumCores", 0);
    cpuInfo.mNumThreads = json.GetValue<size_t>("totalNumThreads", 0);

    err = cpuInfo.mArch.Assign(json.GetValue<std::string>("arch", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse arch");

    if (json.Has("archFamily")) {
        cpuInfo.mArchFamily.EmplaceValue();

        err = cpuInfo.mArchFamily->Assign(json.GetValue<std::string>("archFamily", "").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse archFamily");
    }

    if (json.Has("maxDmips")) {
        cpuInfo.mMaxDMIPS.EmplaceValue(json.GetValue<uint64_t>("maxDmips", 0));
    }
}

Poco::JSON::Object::Ptr NodeAttrToJSON(const NodeAttribute& nodeAttr)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set(nodeAttr.mName.CStr(), nodeAttr.mValue.CStr());

    return json;
}

void NodeAttrFromJSON(const Poco::Dynamic::Var& var, NodeAttribute& nodeAttr)
{
    if (var.isString()) {
        auto err = nodeAttr.mName.Assign(var.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse name");

        return;
    }

    try {
        auto object = var.extract<Poco::JSON::Object::Ptr>();
        if (object->size() != 1) {
            AOS_ERROR_THROW(
                ErrorEnum::eInvalidArgument, "Node attribute JSON object must contain exactly one key-value pair");
        }

        auto err = nodeAttr.mName.Assign(object->begin()->first.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse name");

        err = nodeAttr.mValue.Assign(object->begin()->second.toString().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse value");
    } catch (const std::exception& e) {
        AOS_ERROR_THROW(utils::ToAosError(e), "failed to unparse node attribute");
    }
}

Poco::JSON::Object::Ptr PartitionToJSON(const PartitionInfo& partition)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("name", partition.mName.CStr());
    json->set("types", utils::ToJsonArray(partition.mTypes, [](const auto& type) { return type.CStr(); }));
    json->set("totalSize", partition.mTotalSize);

    return json;
}

void PartitionFromJSON(const utils::CaseInsensitiveObjectWrapper& json, PartitionInfo& partition)
{
    auto err = partition.mName.Assign(json.GetValue<std::string>("name", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse name");

    if (json.Has("types")) {
        auto types = utils::GetArrayValue<std::string>(json, "types");
        for (const auto& type : types) {
            err = partition.mTypes.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to add partition type");

            err = partition.mTypes.Back().Assign(type.c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse partition type");
        }
    }

    partition.mTotalSize = json.GetValue<size_t>("totalSize", 0);
}

Poco::JSON::Object::Ptr NodeInfoToJSON(const NodeInfo& nodeInfo)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("id", nodeInfo.mNodeID.CStr());
    json->set("name", nodeInfo.mName.CStr());
    json->set("type", nodeInfo.mNodeType.CStr());
    json->set("maxDmips", nodeInfo.mMaxDMIPS);
    json->set("cpus", utils::ToJsonArray(nodeInfo.mCPUs, CPUInfoToJSON));
    json->set("osType", nodeInfo.mOSType.CStr());

    if (!nodeInfo.mAttrs.IsEmpty()) {
        json->set("atts", utils::ToJsonArray(nodeInfo.mAttrs, NodeAttrToJSON));
    }

    json->set("totalRAM", nodeInfo.mTotalRAM);
    json->set("partitions", utils::ToJsonArray(nodeInfo.mPartitions, PartitionToJSON));
    json->set("status", nodeInfo.mStatus.ToString().CStr());

    return json;
}

void NodeInfoFromJSON(const utils::CaseInsensitiveObjectWrapper& json, NodeInfo& nodeInfo)
{
    auto err = nodeInfo.mNodeID.Assign(json.GetValue<std::string>("id", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse node ID");

    err = nodeInfo.mName.Assign(json.GetValue<std::string>("name", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse node name");

    err = nodeInfo.mNodeType.Assign(json.GetValue<std::string>("type", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse node type");

    nodeInfo.mMaxDMIPS = json.GetValue<uint64_t>("maxDmips", 0);

    utils::ForEach(json, "cpus", [&nodeInfo](const auto& cpuJson) {
        auto err = nodeInfo.mCPUs.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add CPU info");

        CPUInfoFromJSON(utils::CaseInsensitiveObjectWrapper(cpuJson), nodeInfo.mCPUs.Back());
    });

    err = nodeInfo.mOSType.Assign(json.GetValue<std::string>("osType", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse OS type");

    utils::ForEach(json, "atts", [&nodeInfo](const auto& attrJson) {
        auto err = nodeInfo.mAttrs.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add node attribute");

        NodeAttrFromJSON(attrJson, nodeInfo.mAttrs.Back());
    });

    nodeInfo.mTotalRAM = json.GetValue<size_t>("totalRAM", 0);

    utils::ForEach(json, "partitions", [&nodeInfo](const auto& partitionJson) {
        auto err = nodeInfo.mPartitions.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add partition info");

        PartitionFromJSON(utils::CaseInsensitiveObjectWrapper(partitionJson), nodeInfo.mPartitions.Back());
    });

    err = nodeInfo.mStatus.FromString(json.GetValue<std::string>("status", "").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse status");
}

Poco::JSON::Object::Ptr ServiceStatusToJSON(const ServiceStatus& status)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("id", status.mServiceID.CStr());
    json->set("version", status.mVersion.CStr());
    json->set("status", status.mStatus.ToString().CStr());

    if (!status.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(status.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

Poco::JSON::Object::Ptr InstanceStatusToJSON(const InstanceStatusObsolete& status)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(status.mInstanceIdent, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instance ident to JSON");

    json->set("version", status.mServiceVersion.CStr());
    json->set("nodeId", status.mNodeID.CStr());
    json->set("status", status.mStatus.ToString().CStr());

    if (!status.mStateChecksum.IsEmpty()) {
        json->set("stateChecksum", status.mStateChecksum.CStr());
    }

    if (!status.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        err = ToJSON(status.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

Poco::JSON::Object::Ptr LayerStatusToJSON(const LayerStatus& status)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("id", status.mLayerID.CStr());
    json->set("digest", status.mDigest.CStr());
    json->set("version", status.mVersion.CStr());
    json->set("status", status.mStatus.ToString().CStr());

    if (!status.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(status.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

Poco::JSON::Object::Ptr ComponentStatusToJSON(const aos::cloudprotocol::ComponentStatus& status)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("id", status.mComponentID.CStr());

    if (!status.mComponentType.IsEmpty()) {
        json->set("type", status.mComponentType.CStr());
    }

    json->set("version", status.mVersion.CStr());

    if (status.mNodeID.HasValue()) {
        json->set("nodeId", *status.mNodeID);
    }

    json->set("status", status.mStatus.ToString().CStr());

    if (status.mAnnotations.HasValue()) {
        json->set("annotations", *status.mAnnotations);
    }

    if (!status.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(status.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::UnitStatus& unitStatus)
{
    try {
        utils::ForEach(json, "unitConfig", [&unitStatus](const auto& unitConfigJson) {
            auto err = unitStatus.mUnitConfigStatus.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to add unit config status");

            UnitConfigStatusFromJSON(
                utils::CaseInsensitiveObjectWrapper(unitConfigJson), unitStatus.mUnitConfigStatus.Back());
        });

        utils::ForEach(json, "nodes", [&unitStatus](const auto& nodeJson) {
            auto err = unitStatus.mNodeInfo.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to add node info");

            NodeInfoFromJSON(utils::CaseInsensitiveObjectWrapper(nodeJson), unitStatus.mNodeInfo.Back());
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::UnitStatus& unitStatus, Poco::JSON::Object& json)
{
    try {
        constexpr aos::cloudprotocol::MessageType cMessageType = aos::cloudprotocol::MessageTypeEnum::eUnitStatus;

        json.set("messageType", cMessageType.ToString().CStr());
        json.set("isDeltaInfo", false);

        json.set("unitConfig", utils::ToJsonArray(unitStatus.mUnitConfigStatus, UnitConfigStatusToJSON));
        json.set("nodes", utils::ToJsonArray(unitStatus.mNodeInfo, NodeInfoToJSON));
        json.set("services", utils::ToJsonArray(unitStatus.mServiceStatus, ServiceStatusToJSON));
        json.set("instances", utils::ToJsonArray(unitStatus.mInstanceStatus, InstanceStatusToJSON));
        json.set("layers", utils::ToJsonArray(unitStatus.mLayerStatus, LayerStatusToJSON));
        json.set("components", utils::ToJsonArray(unitStatus.mComponentStatus, ComponentStatusToJSON));
        json.set("unitSubjects", unitStatus.mUnitSubjects.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DeltaUnitStatus& deltaUnitStatus)
{
    try {
        if (json.Has("unitConfig")) {
            deltaUnitStatus.mUnitConfigStatus.EmplaceValue();

            utils::ForEach(json, "unitConfig", [&deltaUnitStatus](const auto& unitConfigJson) {
                auto err = deltaUnitStatus.mUnitConfigStatus->EmplaceBack();
                AOS_ERROR_CHECK_AND_THROW(err, "failed to add unit config status");

                UnitConfigStatusFromJSON(
                    utils::CaseInsensitiveObjectWrapper(unitConfigJson), deltaUnitStatus.mUnitConfigStatus->Back());
            });
        }

        if (json.Has("nodes")) {
            deltaUnitStatus.mNodeInfo.EmplaceValue();

            utils::ForEach(json, "nodes", [&deltaUnitStatus](const auto& nodeJson) {
                auto err = deltaUnitStatus.mNodeInfo->EmplaceBack();
                AOS_ERROR_CHECK_AND_THROW(err, "failed to add node info");

                NodeInfoFromJSON(utils::CaseInsensitiveObjectWrapper(nodeJson), deltaUnitStatus.mNodeInfo->Back());
            });
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::DeltaUnitStatus& deltaUnitStatus, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType = aos::cloudprotocol::MessageTypeEnum::eUnitStatus;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("isDeltaInfo", true);

        if (deltaUnitStatus.mUnitConfigStatus.HasValue()) {
            json.set("unitConfig", utils::ToJsonArray(*deltaUnitStatus.mUnitConfigStatus, UnitConfigStatusToJSON));
        }

        if (deltaUnitStatus.mNodeInfo.HasValue()) {
            json.set("nodes", utils::ToJsonArray(*deltaUnitStatus.mNodeInfo, NodeInfoToJSON));
        }

        if (deltaUnitStatus.mServiceStatus.HasValue()) {
            json.set("services", utils::ToJsonArray(*deltaUnitStatus.mServiceStatus, ServiceStatusToJSON));
        }

        if (deltaUnitStatus.mInstanceStatus.HasValue()) {
            json.set("instances", utils::ToJsonArray(*deltaUnitStatus.mInstanceStatus, InstanceStatusToJSON));
        }

        if (deltaUnitStatus.mLayerStatus.HasValue()) {
            json.set("layers", utils::ToJsonArray(*deltaUnitStatus.mLayerStatus, LayerStatusToJSON));
        }

        if (deltaUnitStatus.mComponentStatus.HasValue()) {
            json.set("components", utils::ToJsonArray(*deltaUnitStatus.mComponentStatus, ComponentStatusToJSON));
        }

        if (deltaUnitStatus.mUnitSubjects.HasValue()) {
            json.set("unitSubjects", *deltaUnitStatus.mUnitSubjects);
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
