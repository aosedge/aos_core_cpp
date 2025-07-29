/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "desiredstatus.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

void DeviceInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, DeviceInfo& deviceInfo)
{
    const auto name = object.GetValue<std::string>("name");

    auto err = deviceInfo.mName.Assign(name.c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "parsed name length exceeds application limit");

    deviceInfo.mSharedCount = object.GetValue<size_t>("sharedCount");

    const auto groups = utils::GetArrayValue<std::string>(object, "groups");

    for (const auto& group : groups) {
        err = deviceInfo.mGroups.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed groups count exceeds application limit");

        err = deviceInfo.mGroups.Back().Assign(group.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed group length exceeds application limit");
    }

    const auto hostDevices = utils::GetArrayValue<std::string>(object, "hostDevices");

    for (const auto& device : hostDevices) {
        err = deviceInfo.mHostDevices.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed host devices count exceeds application limit");

        err = deviceInfo.mHostDevices.Back().Assign(device.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed host device length exceeds application limit");
    }
}

void DevicesFromJSON(const utils::CaseInsensitiveObjectWrapper& object, Array<DeviceInfo>& outDevices)
{
    utils::ForEach(object, "devices", [&outDevices](const auto& value) {
        auto err = outDevices.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed devices count exceeds application limit");

        DeviceInfoFromJSON(utils::CaseInsensitiveObjectWrapper(value), outDevices.Back());
    });
}

void FileSystemMountFromJSON(const utils::CaseInsensitiveObjectWrapper& object, Mount& mount)
{
    auto err = mount.mDestination.Assign(object.GetValue<std::string>("destination").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "parsed destination length exceeds application limit");

    err = mount.mType.Assign(object.GetValue<std::string>("type").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "parsed type length exceeds application limit");

    err = mount.mSource.Assign(object.GetValue<std::string>("source").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "parsed source length exceeds application limit");

    const auto options = utils::GetArrayValue<std::string>(object, "options");

    for (const auto& option : options) {
        err = mount.mOptions.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed options count exceeds application limit");

        err = mount.mOptions.Back().Assign(option.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed option length exceeds application limit");
    }
}

void HostFromJSON(const utils::CaseInsensitiveObjectWrapper& object, Host& host)
{
    auto err = host.mIP.Assign(object.GetValue<std::string>("ip").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "parsed ip length exceeds application limit");

    err = host.mHostname.Assign(object.GetValue<std::string>("hostName").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "parsed hostName length exceeds application limit");
}

void ResourceInfoFromJSON(const utils::CaseInsensitiveObjectWrapper& object, ResourceInfo& resourceInfo)
{
    auto err = resourceInfo.mName.Assign(object.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "parsed name length exceeds application limit");

    const auto groups = utils::GetArrayValue<std::string>(object, "groups");

    for (const auto& group : groups) {
        err = resourceInfo.mGroups.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed groups count exceeds application limit");

        err = resourceInfo.mGroups.Back().Assign(group.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed group length exceeds application limit");
    }

    utils::ForEach(object, "mounts", [&resourceInfo](const auto& value) {
        auto err = resourceInfo.mMounts.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed mounts count exceeds application limit");

        FileSystemMountFromJSON(utils::CaseInsensitiveObjectWrapper(value), resourceInfo.mMounts.Back());
    });

    const auto envs = utils::GetArrayValue<std::string>(object, "env");

    for (const auto& env : envs) {
        err = resourceInfo.mEnv.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed envs count exceeds application limit");

        err = resourceInfo.mEnv.Back().Assign(env.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed env length exceeds application limit");
    }

    utils::ForEach(object, "hosts", [&resourceInfo](const auto& value) {
        auto err = resourceInfo.mHosts.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed hosts count exceeds application limit");

        HostFromJSON(utils::CaseInsensitiveObjectWrapper(value), resourceInfo.mHosts.Back());
    });
}

void ResourcesFromJSON(const utils::CaseInsensitiveObjectWrapper& object, Array<ResourceInfo>& outResources)
{
    utils::ForEach(object, "resources", [&outResources](const auto& value) {
        auto err = outResources.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed resources count exceeds application limit");

        ResourceInfoFromJSON(utils::CaseInsensitiveObjectWrapper(value), outResources.Back());
    });
}

void LabelsFromJSON(const utils::CaseInsensitiveObjectWrapper& object, Array<StaticString<cLabelNameLen>>& outLabels)
{
    const auto labels = utils::GetArrayValue<std::string>(object, "labels");

    for (const auto& label : labels) {
        auto err = outLabels.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed labels count exceeds application limit");

        err = outLabels.Back().Assign(label.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed label length exceeds application limit");
    }
}

std::string ToStdString(const String& str)
{
    return str.CStr();
}

Poco::JSON::Array::Ptr DevicesToJson(const Array<DeviceInfo>& devices)
{
    auto array = Poco::makeShared<Poco::JSON::Array>();

    for (const auto& device : devices) {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("name", device.mName.CStr());
        object->set("sharedCount", device.mSharedCount);
        object->set("groups", utils::ToJsonArray(device.mGroups, ToStdString));
        object->set("hostDevices", utils::ToJsonArray(device.mHostDevices, ToStdString));

        array->add(object);
    }

    return array;
}

Poco::JSON::Array::Ptr MountsToJson(const Array<Mount>& mounts)
{
    auto array = Poco::makeShared<Poco::JSON::Array>();

    for (const auto& mount : mounts) {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("destination", mount.mDestination.CStr());
        object->set("type", mount.mType.CStr());
        object->set("source", mount.mSource.CStr());
        object->set("options", utils::ToJsonArray(mount.mOptions, ToStdString));

        array->add(object);
    }

    return array;
}

Poco::JSON::Array::Ptr HostsToJson(const Array<Host>& hosts)
{
    auto array = Poco::makeShared<Poco::JSON::Array>();

    for (const auto& host : hosts) {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("ip", host.mIP.CStr());
        object->set("hostName", host.mHostname.CStr());

        array->add(object);
    }

    return array;
}

Poco::JSON::Array::Ptr ResourcesToJson(const Array<ResourceInfo>& resources)
{
    auto array = Poco::makeShared<Poco::JSON::Array>();

    for (const auto& resource : resources) {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("name", resource.mName.CStr());
        object->set("groups", utils::ToJsonArray(resource.mGroups, ToStdString));
        object->set("mounts", MountsToJson(resource.mMounts));
        object->set("env", utils::ToJsonArray(resource.mEnv, ToStdString));
        object->set("hosts", HostsToJson(resource.mHosts));

        array->add(object);
    }

    return array;
}

AlertRulePercents AlertRulePercentsFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    AlertRulePercents percents = {};

    if (const auto minTimeout = object.GetOptionalValue<std::string>("minTimeout"); minTimeout.has_value()) {
        Error err;

        // cppcheck-suppress unusedScopedObject
        Tie(percents.mMinTimeout, err) = utils::ParseDuration(minTimeout->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "min timeout parsing error");
    }

    percents.mMinThreshold = object.GetValue<double>("minThreshold");
    percents.mMaxThreshold = object.GetValue<double>("maxThreshold");

    return percents;
}

AlertRulePoints AlertRulePointsFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    AlertRulePoints points = {};

    if (const auto minTimeout = object.GetOptionalValue<std::string>("minTimeout"); minTimeout.has_value()) {
        Error err;

        // cppcheck-suppress unusedScopedObject
        Tie(points.mMinTimeout, err) = utils::ParseDuration(minTimeout->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "min timeout parsing error");
    }

    points.mMinThreshold = object.GetValue<uint64_t>("minThreshold");
    points.mMaxThreshold = object.GetValue<uint64_t>("maxThreshold");

    return points;
}

PartitionAlertRule PartitionAlertRuleFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    const auto name = object.GetValue<std::string>("name");

    return {AlertRulePercentsFromJSON(object), name.c_str()};
}

AlertRules AlertRulesFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    AlertRules rules = {};

    if (object.Has("ram")) {
        rules.mRAM.SetValue(AlertRulePercentsFromJSON(object.GetObject("ram")));
    }

    if (object.Has("cpu")) {
        rules.mCPU.SetValue(AlertRulePercentsFromJSON(object.GetObject("cpu")));
    }

    if (object.Has("partitions")) {
        auto partitions = utils::GetArrayValue<PartitionAlertRule>(object, "partitions",
            [](const auto& value) { return PartitionAlertRuleFromJSON(utils::CaseInsensitiveObjectWrapper(value)); });

        for (const auto& partition : partitions) {
            auto err = rules.mPartitions.PushBack(partition);
            AOS_ERROR_CHECK_AND_THROW(err, "partition alert rules parsing error");
        }
    }

    if (object.Has("download")) {
        rules.mDownload.SetValue(AlertRulePointsFromJSON(object.GetObject("download")));
    }

    if (object.Has("upload")) {
        rules.mUpload.SetValue(AlertRulePointsFromJSON(object.GetObject("upload")));
    }

    return rules;
}

template <class T>
Poco::JSON::Object::Ptr AlertRuleToJSON(const T& rule)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (rule.mMinTimeout > 0) {
        auto duration = rule.mMinTimeout.ToISO8601String();

        object->set("minTimeout", duration.CStr());
    }

    object->set("minThreshold", rule.mMinThreshold);
    object->set("maxThreshold", rule.mMaxThreshold);

    return object;
}

template <>
Poco::JSON::Object::Ptr AlertRuleToJSON(const PartitionAlertRule& rule)
{
    auto object = AlertRuleToJSON<AlertRulePercents>(rule);

    object->set("name", rule.mName.CStr());

    return object;
}

Poco::JSON::Object::Ptr AlertRulesToJSON(const AlertRules& rules)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (rules.mRAM.HasValue()) {
        object->set("ram", AlertRuleToJSON(rules.mRAM.GetValue()));
    }

    if (rules.mCPU.HasValue()) {
        object->set("cpu", AlertRuleToJSON(rules.mCPU.GetValue()));
    }

    if (rules.mDownload.HasValue()) {
        object->set("download", AlertRuleToJSON(rules.mDownload.GetValue()));
    }

    if (rules.mUpload.HasValue()) {
        object->set("upload", AlertRuleToJSON(rules.mUpload.GetValue()));
    }

    object->set("partitions", utils::ToJsonArray(rules.mPartitions, AlertRuleToJSON<PartitionAlertRule>));

    return object;
}

ResourceRatios ResourceRatiosFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    ResourceRatios ratios = {};

    if (object.Has("cpu")) {
        ratios.mCPU.SetValue(object.GetValue<double>("cpu"));
    }

    if (object.Has("ram")) {
        ratios.mRAM.SetValue(object.GetValue<double>("ram"));
    }

    if (object.Has("storage")) {
        ratios.mStorage.SetValue(object.GetValue<double>("storage"));
    }

    if (object.Has("state")) {
        ratios.mState.SetValue(object.GetValue<double>("state"));
    }

    return ratios;
}

Poco::JSON::Object::Ptr ResourceRatiosToJSON(const ResourceRatios& ratios)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (ratios.mCPU.HasValue()) {
        object->set("cpu", ratios.mCPU.GetValue());
    }

    if (ratios.mRAM.HasValue()) {
        object->set("ram", ratios.mRAM.GetValue());
    }

    if (ratios.mStorage.HasValue()) {
        object->set("storage", ratios.mStorage.GetValue());
    }

    if (ratios.mState.HasValue()) {
        object->set("state", ratios.mState.GetValue());
    }

    return object;
}

void UnitConfigFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::UnitConfig& unitConfig)
{
    auto err = unitConfig.mVersion.Assign(json.GetValue<std::string>("version").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse version from JSON");

    err = unitConfig.mFormatVersion.Assign(json.GetValue<std::string>("formatVersion").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse formatVersion from JSON");

    utils::ForEach(json, "nodes", [&unitConfig](const auto& value) {
        auto err = unitConfig.mNodes.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "parsed nodes count exceeds application limit");

        err = FromJSON(utils::CaseInsensitiveObjectWrapper(value), unitConfig.mNodes.Back());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse node config from JSON");
    });
}

Poco::JSON::Object::Ptr UnitConfigToJSON(const aos::cloudprotocol::UnitConfig& unitConfig)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    object->set("version", unitConfig.mVersion.CStr());
    object->set("formatVersion", unitConfig.mFormatVersion.CStr());
    object->set("nodes", utils::ToJsonArray(unitConfig.mNodes, [](const auto& node) {
        auto nodeObject = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(node, *nodeObject);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert node config to JSON");

        return nodeObject;
    }));

    return object;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::NodeConfig& nodeConfig)
{
    try {
        auto err = nodeConfig.mNodeType.Assign(json.GetValue<std::string>("nodeType").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed nodeType length exceeds application limit");

        nodeConfig.mPriority = json.GetValue<uint32_t>("priority");

        DevicesFromJSON(json, nodeConfig.mDevices);
        ResourcesFromJSON(json, nodeConfig.mResources);
        LabelsFromJSON(json, nodeConfig.mLabels);

        if (json.Has("alertRules")) {
            nodeConfig.mAlertRules.SetValue(AlertRulesFromJSON(json.GetObject("alertRules")));
        }

        if (json.Has("resourceRatios")) {
            nodeConfig.mResourceRatios.SetValue(ResourceRatiosFromJSON(json.GetObject("resourceRatios")));
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::NodeConfig& nodeConfig, Poco::JSON::Object& json)
{
    try {
        json.set("nodeType", nodeConfig.mNodeType.CStr());
        json.set("priority", nodeConfig.mPriority);
        json.set("devices", DevicesToJson(nodeConfig.mDevices));
        json.set("resources", ResourcesToJson(nodeConfig.mResources));
        json.set("labels", utils::ToJsonArray(nodeConfig.mLabels, ToStdString));

        if (nodeConfig.mAlertRules.HasValue()) {
            json.set("alertRules", AlertRulesToJSON(*nodeConfig.mAlertRules));
        }

        if (nodeConfig.mResourceRatios.HasValue()) {
            json.set("resourceRatios", ResourceRatiosToJSON(*nodeConfig.mResourceRatios));
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DesiredStatus& desiredStatus)
{
    try {
        if (json.Has("unitConfig")) {
            desiredStatus.mUnitConfig.EmplaceValue();

            UnitConfigFromJSON(json.GetObject("unitConfig"), *desiredStatus.mUnitConfig);
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::DesiredStatus& desiredStatus, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType = aos::cloudprotocol::MessageTypeEnum::eDesiredStatus;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (desiredStatus.mUnitConfig.HasValue()) {
            json.set("unitConfig", UnitConfigToJSON(*desiredStatus.mUnitConfig));
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
