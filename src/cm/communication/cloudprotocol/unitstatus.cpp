/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "unitstatus.hpp"

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr UnitConfigToJSON(const UnitConfigStatus& unitConfigStatus)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("version", unitConfigStatus.mVersion.CStr());
    json->set("state", unitConfigStatus.mState.ToString().CStr());

    if (!unitConfigStatus.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(unitConfigStatus.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

Poco::JSON::Object::Ptr ArchInfoToJSON(const ArchInfo& archInfo)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("architecture", archInfo.mArchitecture.CStr());

    if (archInfo.mVariant.HasValue()) {
        json->set("variant", archInfo.mVariant->CStr());
    }

    return json;
}

Poco::JSON::Object::Ptr CPUInfoToJSON(const CPUInfo& cpuInfo)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("modelName", cpuInfo.mModelName.CStr());
    json->set("totalNumCores", cpuInfo.mNumCores);
    json->set("totalNumThreads", cpuInfo.mNumThreads);
    json->set("archInfo", ArchInfoToJSON(cpuInfo.mArchInfo));

    if (cpuInfo.mMaxDMIPS.HasValue()) {
        json->set("maxDmips", *cpuInfo.mMaxDMIPS);
    }

    return json;
}

Poco::JSON::Object::Ptr PartitionToJSON(const PartitionInfo& partition)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("name", partition.mName.CStr());
    json->set("types", common::utils::ToJsonArray(partition.mTypes, [](const auto& type) { return type.CStr(); }));
    json->set("totalSize", partition.mTotalSize);

    return json;
}

Poco::JSON::Object::Ptr OSInfoToJSON(const OSInfo& osInfo)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("os", osInfo.mOS.CStr());

    if (osInfo.mVersion.HasValue()) {
        json->set("version", osInfo.mVersion->CStr());
    }

    if (!osInfo.mFeatures.IsEmpty()) {
        json->set("features",
            common::utils::ToJsonArray(osInfo.mFeatures, [](const auto& feature) { return feature.CStr(); }));
    }

    return json;
}

Poco::JSON::Object::Ptr NodeAttrsToJSON(const Array<NodeAttribute>& attrs)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    for (const auto& attr : attrs) {
        json->set(attr.mName.CStr(), attr.mValue.CStr());
    }

    return json;
}

Poco::JSON::Object::Ptr RuntimeInfoToJSON(const RuntimeInfo& runtimeInfo)
{
    AosIdentity identity;
    identity.mCodename = runtimeInfo.mRuntimeID.CStr();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("identity", CreateAosIdentity(identity));
    json->set("runtimeType", runtimeInfo.mRuntimeType.CStr());
    json->set("archInfo", ArchInfoToJSON(runtimeInfo.mArchInfo));
    json->set("osInfo", OSInfoToJSON(runtimeInfo.mOSInfo));

    if (runtimeInfo.mMaxDMIPS.HasValue()) {
        json->set("maxDmips", *runtimeInfo.mMaxDMIPS);
    }

    if (runtimeInfo.mAllowedDMIPS.HasValue()) {
        json->set("allowedDmips", *runtimeInfo.mAllowedDMIPS);
    }

    if (runtimeInfo.mTotalRAM.HasValue()) {
        json->set("totalRam", *runtimeInfo.mTotalRAM);
    }

    if (runtimeInfo.mAllowedRAM.HasValue()) {
        json->set("allowedRam", *runtimeInfo.mAllowedRAM);
    }

    json->set("maxInstances", runtimeInfo.mMaxInstances);

    return json;
}

Poco::JSON::Object::Ptr ResourceInfoToJSON(const ResourceInfo& resourceInfo)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("name", resourceInfo.mName.CStr());
    json->set("sharedCount", resourceInfo.mSharedCount);

    return json;
}

Poco::JSON::Object::Ptr NodeInfoToJSON(const UnitNodeInfo& nodeInfo)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    {
        AosIdentity identity;
        identity.mCodename = nodeInfo.mNodeID.CStr();
        identity.mTitle    = nodeInfo.mTitle.CStr();

        json->set("identity", CreateAosIdentity(identity));
    }

    {
        AosIdentity identity;
        identity.mCodename = nodeInfo.mNodeType.CStr();

        json->set("nodeGroupSubject", CreateAosIdentity(identity));
    }

    json->set("maxDmips", nodeInfo.mMaxDMIPS);

    if (nodeInfo.mPhysicalRAM.HasValue()) {
        json->set("physicalRam", *nodeInfo.mPhysicalRAM);
    }

    json->set("totalRam", nodeInfo.mTotalRAM);
    json->set("osInfo", OSInfoToJSON(nodeInfo.mOSInfo));

    if (!nodeInfo.mCPUs.IsEmpty()) {
        json->set("cpus", common::utils::ToJsonArray(nodeInfo.mCPUs, CPUInfoToJSON));
    }

    if (!nodeInfo.mAttrs.IsEmpty()) {
        json->set("atts", NodeAttrsToJSON(nodeInfo.mAttrs));
    }

    if (!nodeInfo.mPartitions.IsEmpty()) {
        json->set("partitions", common::utils::ToJsonArray(nodeInfo.mPartitions, PartitionToJSON));
    }

    if (!nodeInfo.mRuntimes.IsEmpty()) {
        json->set("runtimes", common::utils::ToJsonArray(nodeInfo.mRuntimes, RuntimeInfoToJSON));
    }

    if (!nodeInfo.mResources.IsEmpty()) {
        json->set("resources", common::utils::ToJsonArray(nodeInfo.mResources, ResourceInfoToJSON));
    }

    json->set("state", nodeInfo.mState.ToString().CStr());
    json->set("isConnected", nodeInfo.mIsConnected);

    if (!nodeInfo.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(nodeInfo.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

Poco::JSON::Object::Ptr UpdateItemToJSON(const UpdateItemStatus& status)
{
    AosIdentity identity;
    identity.mID = status.mItemID.CStr();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("item", CreateAosIdentity(identity));
    json->set("version", status.mVersion.CStr());
    json->set("state", status.mState.ToString().CStr());

    if (!status.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(status.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

Poco::JSON::Object::Ptr InstanceToJSON(const UnitInstancesStatuses& statuses)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    const auto isPreinstalled = statuses.mInstances.FindIf([](const UnitInstanceStatus& status) {
        return status.mPreinstalled;
    }) != statuses.mInstances.end();

    {
        AosIdentity identity;

        if (isPreinstalled) {
            identity.mCodename = statuses.mItemID.CStr();
        } else {
            identity.mID = statuses.mItemID.CStr();
        }

        json->set("item", CreateAosIdentity(identity));
    }

    {
        AosIdentity identity;

        if (isPreinstalled) {
            identity.mCodename = statuses.mSubjectID.CStr();
        } else {
            identity.mID = statuses.mSubjectID.CStr();
        }

        json->set("subject", CreateAosIdentity(identity));
    }

    json->set("version", statuses.mVersion.CStr());

    auto instancesJson = Poco::makeShared<Poco::JSON::Array>(Poco::JSON_PRESERVE_KEY_ORDER);

    for (const auto& instanceStatus : statuses.mInstances) {
        auto instanceJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        {
            AosIdentity identity;
            identity.mCodename = instanceStatus.mNodeID.CStr();

            instanceJson->set("node", CreateAosIdentity(identity));
        }

        {
            AosIdentity identity;
            identity.mCodename = instanceStatus.mRuntimeID.CStr();

            instanceJson->set("runtime", CreateAosIdentity(identity));
        }

        instanceJson->set("instance", instanceStatus.mInstance);

        if (!instanceStatus.mStateChecksum.IsEmpty()) {
            StaticString<crypto::cSHA256Size * 2> checksum;

            auto err = checksum.ByteArrayToHex(instanceStatus.mStateChecksum);
            AOS_ERROR_CHECK_AND_THROW(err, "can't convert state checksum to JSON");

            instanceJson->set("stateChecksum", checksum.CStr());
        }

        instanceJson->set("state", instanceStatus.mState.ToString().CStr());

        if (!instanceStatus.mError.IsNone()) {
            auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(instanceStatus.mError, *errorInfo);
            AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

            instanceJson->set("errorInfo", errorInfo);
        }

        instancesJson->add(instanceJson);
    }

    json->set("instances", instancesJson);

    return json;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ToJSON(const UnitStatus& unitStatus, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eUnitStatus;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(unitStatus), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        json.set("isDeltaInfo", unitStatus.mIsDeltaInfo);

        if (unitStatus.mUnitConfig.HasValue()) {
            json.set("unitConfig", common::utils::ToJsonArray(*unitStatus.mUnitConfig, UnitConfigToJSON));
        }

        if (unitStatus.mNodes.HasValue()) {
            json.set("nodes", common::utils::ToJsonArray(*unitStatus.mNodes, NodeInfoToJSON));
        }

        if (unitStatus.mUpdateItems.HasValue()) {
            json.set("items", common::utils::ToJsonArray(*unitStatus.mUpdateItems, UpdateItemToJSON));
        }

        if (unitStatus.mInstances.HasValue()) {
            json.set("instances", common::utils::ToJsonArray(*unitStatus.mInstances, InstanceToJSON));
        }

        if (unitStatus.mUnitSubjects.HasValue()) {
            json.set("subjects", common::utils::ToJsonArray(*unitStatus.mUnitSubjects, [](const auto& subject) {
                AosIdentity identity;
                identity.mCodename = subject.CStr();

                return CreateAosIdentity(identity);
            }));
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
