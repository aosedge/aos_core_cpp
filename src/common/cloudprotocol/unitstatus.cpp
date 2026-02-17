/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

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

Poco::JSON::Object::Ptr UnitConfigToJSON(const UnitConfigStatus& unitConfigStatus)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (!unitConfigStatus.mVersion.IsEmpty()) {
        json->set("version", unitConfigStatus.mVersion.CStr());
    }

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

    if (!partition.mPath.IsEmpty()) {
        json->set("path", partition.mPath.CStr());
    }

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

    auto err = ToJSON(nodeInfo, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "can't convert NodeInfo to JSON");

    if (!nodeInfo.mRuntimes.IsEmpty()) {
        json->set("runtimes", common::utils::ToJsonArray(nodeInfo.mRuntimes, RuntimeInfoToJSON));
    }

    if (!nodeInfo.mResources.IsEmpty()) {
        json->set("resources", common::utils::ToJsonArray(nodeInfo.mResources, ResourceInfoToJSON));
    }

    return json;
}

Poco::JSON::Object::Ptr UpdateItemToJSON(const UpdateItemStatus& status)
{
    AosIdentity identity;
    identity.mID   = status.mItemID.CStr();
    identity.mType = status.mType;

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

    {
        AosIdentity identity;

        if (statuses.mPreinstalled) {
            identity.mCodename = statuses.mItemID.CStr();
        } else {
            identity.mID = statuses.mItemID.CStr();
        }

        json->set("item", CreateAosIdentity(identity));
    }

    {
        AosIdentity identity;

        if (statuses.mPreinstalled) {
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

void OSInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, OSInfo& dst)
{
    auto err = dst.mOS.Assign(object.GetValue<std::string>("os").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse OS name");

    if (auto version = object.GetOptionalValue<std::string>("version"); version.has_value()) {
        dst.mVersion.EmplaceValue();

        err = dst.mVersion->Assign(version->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse OS version");
    }

    common::utils::ForEach(object, "features", [&dst](const Poco::Dynamic::Var& value) {
        auto err = dst.mFeatures.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse feature");

        err = dst.mFeatures.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse feature");
    });
}

void ArchInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, ArchInfo& dst)
{
    auto err = dst.mArchitecture.Assign(object.GetValue<std::string>("architecture").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse architecture");

    if (auto variant = object.GetOptionalValue<std::string>("variant"); variant.has_value()) {
        dst.mVariant.EmplaceValue();

        err = dst.mVariant->Assign(variant->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse architecture variant");
    }
}

void CPUInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, CPUInfo& dst)
{
    auto err = dst.mModelName.Assign(object.GetValue<std::string>("modelName").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse CPU model name");

    dst.mNumCores   = object.GetValue<size_t>("totalNumCores");
    dst.mNumThreads = object.GetValue<size_t>("totalNumThreads");

    if (!object.Has("archInfo")) {
        AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "can't parse ArchInfo");
    }

    ArchInfoFromJSON(object.GetObject("archInfo"), dst.mArchInfo);

    if (auto maxDMIPS = object.GetOptionalValue<size_t>("maxDMIPS"); maxDMIPS.has_value()) {
        dst.mMaxDMIPS.SetValue(maxDMIPS.value());
    }
}

void PartitionInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, PartitionInfo& dst)
{
    auto err = dst.mName.Assign(object.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse partition name");

    common::utils::ForEach(object, "types", [&dst](const Poco::Dynamic::Var& value) {
        auto err = dst.mTypes.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse partition type");

        err = dst.mTypes.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse partition type");
    });

    if (object.Has("path")) {
        err = dst.mPath.Assign(object.GetValue<std::string>("path").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse partition path");
    }

    dst.mTotalSize = object.GetValue<size_t>("totalSize");
}

void NodeAttrsFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, Array<NodeAttribute>& dst)
{
    for (const auto& name : object.GetNames()) {
        auto err = dst.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node attribute");

        err = dst.Back().mName.Assign(name.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse attribute name");

        err = dst.Back().mValue.Assign(object.GetValue<std::string>(name).c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse attribute value");
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ToJSON(const NodeInfo& nodeInfo, Poco::JSON::Object& json)
{
    try {
        {
            AosIdentity identity;
            identity.mCodename = nodeInfo.mNodeID.CStr();
            identity.mTitle    = nodeInfo.mTitle.CStr();

            json.set("identity", CreateAosIdentity(identity));
        }

        {
            AosIdentity identity;
            identity.mCodename = nodeInfo.mNodeType.CStr();

            json.set("nodeGroupSubject", CreateAosIdentity(identity));
        }

        json.set("maxDmips", nodeInfo.mMaxDMIPS);

        if (nodeInfo.mPhysicalRAM.HasValue()) {
            json.set("physicalRam", *nodeInfo.mPhysicalRAM);
        }

        json.set("totalRam", nodeInfo.mTotalRAM);
        json.set("osInfo", OSInfoToJSON(nodeInfo.mOSInfo));

        if (!nodeInfo.mCPUs.IsEmpty()) {
            json.set("cpus", common::utils::ToJsonArray(nodeInfo.mCPUs, CPUInfoToJSON));
        }

        if (!nodeInfo.mAttrs.IsEmpty()) {
            json.set("attrs", NodeAttrsToJSON(nodeInfo.mAttrs));
        }

        if (!nodeInfo.mPartitions.IsEmpty()) {
            json.set("partitions", common::utils::ToJsonArray(nodeInfo.mPartitions, PartitionToJSON));
        }

        json.set("state", nodeInfo.mState.ToString().CStr());
        json.set("isConnected", nodeInfo.mIsConnected);

        if (!nodeInfo.mError.IsNone()) {
            auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(nodeInfo.mError, *errorInfo);
            AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

            json.set("errorInfo", errorInfo);
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, NodeInfo& dst)
{
    try {
        AosIdentity identity;

        auto err = ParseAosIdentity(object.GetObject("identity"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node identity");

        if (!identity.mCodename.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "item codename is missing");
        }

        err = dst.mNodeID.Assign(identity.mCodename->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse nodeID");

        err = dst.mTitle.Assign(identity.mTitle.value_or("").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node title");

        err = ParseAosIdentity(object.GetObject("nodeGroupSubject"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node group subject");

        if (!identity.mCodename.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "item codename is missing");
        }

        err = dst.mNodeType.Assign(identity.mCodename->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node type");

        dst.mMaxDMIPS = object.GetValue<size_t>("maxDMIPS");
        dst.mTotalRAM = object.GetValue<size_t>("totalRAM");

        if (auto physicalRAM = object.GetOptionalValue<size_t>("physicalRAM"); physicalRAM.has_value()) {
            dst.mPhysicalRAM.SetValue(physicalRAM.value());
        }

        if (!object.Has("osInfo")) {
            AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "can't parse OSInfo");
        }

        OSInfoFromJSON(object.GetObject("osInfo"), dst.mOSInfo);

        common::utils::ForEach(object, "cpus", [&dst](const Poco::Dynamic::Var& value) {
            auto err = dst.mCPUs.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse CPU info");

            CPUInfoFromJSON(common::utils::CaseInsensitiveObjectWrapper(value), dst.mCPUs.Back());
        });

        common::utils::ForEach(object, "partitions", [&dst](const Poco::Dynamic::Var& value) {
            auto err = dst.mPartitions.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse Partition info");

            PartitionInfoFromJSON(common::utils::CaseInsensitiveObjectWrapper(value), dst.mPartitions.Back());
        });

        if (object.Has("attrs")) {
            NodeAttrsFromJSON(common::utils::CaseInsensitiveObjectWrapper(object.GetObject("attrs")), dst.mAttrs);
        }

        dst.mIsConnected = object.GetValue<bool>("isConnected");

        err = dst.mState.FromString(object.GetValue<std::string>("state").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node state");

        if (object.Has("errorInfo")) {
            err = FromJSON(object.GetObject("errorInfo"), dst.mError);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse errorInfo");
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }
}

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

} // namespace aos::common::cloudprotocol
