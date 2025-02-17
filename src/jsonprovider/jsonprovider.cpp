/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utils/exception.hpp>
#include <utils/json.hpp>
#include <utils/time.hpp>

#include <Poco/JSON/Parser.h>

#include "jsonprovider/jsonprovider.hpp"

namespace aos::common::jsonprovider {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

void DeviceInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, DeviceInfo& deviceInfo)
{
    const auto name = object.GetValue<std::string>("name");

    auto err = deviceInfo.mName.Assign(name.c_str());
    AOS_ERROR_CHECK_AND_THROW("parsed name length exceeds application limit", err);

    deviceInfo.mSharedCount = object.GetValue<size_t>("sharedCount");

    const auto groups = utils::GetArrayValue<std::string>(object, "groups");

    for (const auto& group : groups) {
        err = deviceInfo.mGroups.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW("parsed groups count exceeds application limit", err);

        err = deviceInfo.mGroups.Back().Assign(group.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed group length exceeds application limit", err);
    }

    const auto hostDevices = utils::GetArrayValue<std::string>(object, "hostDevices");

    for (const auto& device : hostDevices) {
        err = deviceInfo.mHostDevices.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW("parsed host devices count exceeds application limit", err);

        err = deviceInfo.mHostDevices.Back().Assign(device.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed host device length exceeds application limit", err);
    }
}

void DevicesFromJSON(const utils::CaseInsensitiveObjectWrapper& object, Array<DeviceInfo>& outDevices)
{
    utils::ForEach(object, "devices", [&outDevices](const auto& value) {
        auto err = outDevices.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW("parsed devices count exceeds application limit", err);

        DeviceInfoFromJSON(utils::CaseInsensitiveObjectWrapper(value), outDevices.Back());
    });
}

Mount FileSystemMountFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    Mount mount;

    auto err = mount.mDestination.Assign(object.GetValue<std::string>("destination").c_str());
    AOS_ERROR_CHECK_AND_THROW("parsed destination length exceeds application limit", err);

    err = mount.mType.Assign(object.GetValue<std::string>("type").c_str());
    AOS_ERROR_CHECK_AND_THROW("parsed type length exceeds application limit", err);

    err = mount.mSource.Assign(object.GetValue<std::string>("source").c_str());
    AOS_ERROR_CHECK_AND_THROW("parsed source length exceeds application limit", err);

    const auto options = utils::GetArrayValue<std::string>(object, "options");

    for (const auto& option : options) {
        err = mount.mOptions.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW("parsed options count exceeds application limit", err);

        err = mount.mOptions.Back().Assign(option.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed option length exceeds application limit", err);
    }

    return mount;
}

Host HostFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    Host host;

    auto err = host.mIP.Assign(object.GetValue<std::string>("ip").c_str());
    AOS_ERROR_CHECK_AND_THROW("parsed ip length exceeds application limit", err);

    err = host.mHostname.Assign(object.GetValue<std::string>("hostName").c_str());
    AOS_ERROR_CHECK_AND_THROW("parsed hostName length exceeds application limit", err);

    return host;
}

void ResourceInfoFromJSON(const utils::CaseInsensitiveObjectWrapper& object, ResourceInfo& resourceInfo)
{
    auto err = resourceInfo.mName.Assign(object.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW("parsed name length exceeds application limit", err);

    const auto groups = utils::GetArrayValue<std::string>(object, "groups");

    for (const auto& group : groups) {
        err = resourceInfo.mGroups.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW("parsed groups count exceeds application limit", err);

        err = resourceInfo.mGroups.Back().Assign(group.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed group length exceeds application limit", err);
    }

    const auto mounts = utils::GetArrayValue<Mount>(object, "mounts",
        [](const auto& value) { return FileSystemMountFromJSON(utils::CaseInsensitiveObjectWrapper(value)); });

    for (const auto& mount : mounts) {
        err = resourceInfo.mMounts.PushBack(mount);
        AOS_ERROR_CHECK_AND_THROW("parsed mounts count exceeds application limit", err);
    }

    const auto envs = utils::GetArrayValue<std::string>(object, "env");

    for (const auto& env : envs) {
        err = resourceInfo.mEnv.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW("parsed envs count exceeds application limit", err);

        err = resourceInfo.mEnv.Back().Assign(env.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed env length exceeds application limit", err);
    }

    const auto hosts = utils::GetArrayValue<Host>(
        object, "hosts", [](const auto& value) { return HostFromJSON(utils::CaseInsensitiveObjectWrapper(value)); });

    for (const auto& host : hosts) {
        err = resourceInfo.mHosts.PushBack(host);
        AOS_ERROR_CHECK_AND_THROW("parsed hosts count exceeds application limit", err);
    }
}

void ResourcesFromJSON(const utils::CaseInsensitiveObjectWrapper& object, Array<ResourceInfo>& outResources)
{
    utils::ForEach(object, "resources", [&outResources](const auto& value) {
        auto err = outResources.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW("parsed resources count exceeds application limit", err);

        ResourceInfoFromJSON(utils::CaseInsensitiveObjectWrapper(value), outResources.Back());
    });
}

void LabelsFromJSON(const utils::CaseInsensitiveObjectWrapper& object, Array<StaticString<cLabelNameLen>>& outLabels)
{
    const auto labels = utils::GetArrayValue<std::string>(object, "labels");

    for (const auto& label : labels) {
        auto err = outLabels.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW("parsed labels count exceeds application limit", err);

        err = outLabels.Back().Assign(label.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed label length exceeds application limit", err);
    }
}

template <size_t cMaxSize>
Poco::JSON::Array ToJSONArray(const Array<StaticString<cMaxSize>>& aosArray)
{
    Poco::JSON::Array array;

    for (const auto& group : aosArray) {
        array.add(group.CStr());
    }

    return array;
}

Poco::JSON::Array DevicesToJson(const Array<DeviceInfo>& devices)
{
    Poco::JSON::Array array;

    for (const auto& device : devices) {
        Poco::JSON::Object object;

        object.set("name", device.mName.CStr());
        object.set("sharedCount", device.mSharedCount);
        object.set("groups", ToJSONArray(device.mGroups));
        object.set("hostDevices", ToJSONArray(device.mHostDevices));

        array.add(object);
    }

    return array;
}

Poco::JSON::Array MountsToJson(const Array<Mount>& mounts)
{
    Poco::JSON::Array array;

    for (const auto& mount : mounts) {
        Poco::JSON::Object object;

        object.set("destination", mount.mDestination.CStr());
        object.set("type", mount.mType.CStr());
        object.set("source", mount.mSource.CStr());
        object.set("options", ToJSONArray(mount.mOptions));

        array.add(object);
    }

    return array;
}

Poco::JSON::Array HostsToJson(const Array<Host>& hosts)
{
    Poco::JSON::Array array;

    for (const auto& host : hosts) {
        Poco::JSON::Object object;

        object.set("ip", host.mIP.CStr());
        object.set("hostName", host.mHostname.CStr());

        array.add(object);
    }

    return array;
}

Poco::JSON::Array ResourcesToJson(const Array<ResourceInfo>& resources)
{
    Poco::JSON::Array array;

    for (const auto& resource : resources) {
        Poco::JSON::Object object;

        object.set("name", resource.mName.CStr());
        object.set("groups", ToJSONArray(resource.mGroups));
        object.set("mounts", MountsToJson(resource.mMounts));
        object.set("env", ToJSONArray(resource.mEnv));
        object.set("hosts", HostsToJson(resource.mHosts));

        array.add(object);
    }

    return array;
}

AlertRulePercents AlertRulePercentsFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    AlertRulePercents percents = {};

    if (const auto minTimeout = object.GetOptionalValue<std::string>("minTimeout"); minTimeout.has_value()) {
        auto [duration, err] = utils::ParseDuration(minTimeout->c_str());
        AOS_ERROR_CHECK_AND_THROW("min timeout parsing error", err);

        percents.mMinTimeout = duration.count();
    }

    percents.mMinThreshold = object.GetValue<double>("minThreshold");
    percents.mMaxThreshold = object.GetValue<double>("maxThreshold");

    return percents;
}

AlertRulePoints AlertRulePointsFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    AlertRulePoints points = {};

    if (const auto minTimeout = object.GetOptionalValue<std::string>("minTimeout"); minTimeout.has_value()) {
        auto [duration, err] = utils::ParseDuration(minTimeout->c_str());
        AOS_ERROR_CHECK_AND_THROW("min timeout parsing error", err);

        points.mMinTimeout = duration.count();
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
            AOS_ERROR_CHECK_AND_THROW("partition alert rules parsing error", err);
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
Poco::JSON::Object AlertRuleToJSON(const T& rule)
{
    Poco::JSON::Object object;

    if (rule.mMinTimeout > 0) {
        auto [duration, err] = utils::FormatISO8601Duration(utils::Duration(rule.mMinTimeout));
        AOS_ERROR_CHECK_AND_THROW("offlineTTL formatting error", err);

        object.set("minTimeout", duration);
    }

    object.set("minThreshold", rule.mMinThreshold);
    object.set("maxThreshold", rule.mMaxThreshold);

    return object;
}

template <>
Poco::JSON::Object AlertRuleToJSON(const PartitionAlertRule& rule)
{
    Poco::JSON::Object object = AlertRuleToJSON<AlertRulePercents>(rule);

    object.set("name", rule.mName.CStr());

    return object;
}

Poco::JSON::Object AlertRulesToJSON(const AlertRules& rules)
{
    Poco::JSON::Object object;

    if (rules.mRAM.HasValue()) {
        object.set("ram", AlertRuleToJSON(rules.mRAM.GetValue()));
    }

    if (rules.mCPU.HasValue()) {
        object.set("cpu", AlertRuleToJSON(rules.mCPU.GetValue()));
    }

    if (rules.mDownload.HasValue()) {
        object.set("download", AlertRuleToJSON(rules.mDownload.GetValue()));
    }

    if (rules.mUpload.HasValue()) {
        object.set("upload", AlertRuleToJSON(rules.mUpload.GetValue()));
    }

    object.set("partitions", utils::ToJsonArray(rules.mPartitions, AlertRuleToJSON<PartitionAlertRule>));

    return object;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error JSONProvider::NodeConfigToJSON(const sm::resourcemanager::NodeConfig& nodeConfig, String& json) const
{
    try {
        Poco::JSON::Object object;

        object.set("version", nodeConfig.mVersion.CStr());
        object.set("nodeType", nodeConfig.mNodeConfig.mNodeType.CStr());
        object.set("priority", nodeConfig.mNodeConfig.mPriority);
        object.set("devices", DevicesToJson(nodeConfig.mNodeConfig.mDevices));
        object.set("resources", ResourcesToJson(nodeConfig.mNodeConfig.mResources));
        object.set("labels", ToJSONArray(nodeConfig.mNodeConfig.mLabels));

        if (nodeConfig.mNodeConfig.mAlertRules.HasValue()) {
            object.set("alertRules", AlertRulesToJSON(*nodeConfig.mNodeConfig.mAlertRules));
        }

        json = utils::Stringify(object).c_str();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error JSONProvider::NodeConfigFromJSON(const String& json, sm::resourcemanager::NodeConfig& nodeConfig) const
{
    try {
        Poco::JSON::Parser                  parser;
        auto                                result = parser.parse(json.CStr());
        utils::CaseInsensitiveObjectWrapper object(result.extract<Poco::JSON::Object::Ptr>());

        auto err = nodeConfig.mVersion.Assign(object.GetValue<std::string>("version").c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed version length exceeds application limit", err);

        err = nodeConfig.mNodeConfig.mNodeType.Assign(object.GetValue<std::string>("nodeType").c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed nodeType length exceeds application limit", err);

        nodeConfig.mNodeConfig.mPriority = object.GetValue<uint32_t>("priority");

        DevicesFromJSON(object, nodeConfig.mNodeConfig.mDevices);
        ResourcesFromJSON(object, nodeConfig.mNodeConfig.mResources);
        LabelsFromJSON(object, nodeConfig.mNodeConfig.mLabels);

        if (object.Has("alertRules")) {
            nodeConfig.mNodeConfig.mAlertRules.SetValue(AlertRulesFromJSON(object.GetObject("alertRules")));
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::jsonprovider
