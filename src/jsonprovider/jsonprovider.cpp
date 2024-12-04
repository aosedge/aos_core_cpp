/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utils/exception.hpp>
#include <utils/json.hpp>

#include <Poco/JSON/Parser.h>

#include "jsonprovider/jsonprovider.hpp"

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

aos::DeviceInfo DeviceInfoFromJSON(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    aos::DeviceInfo deviceInfo;

    const auto name = object.GetValue<std::string>("name");

    deviceInfo.mName        = name.c_str();
    deviceInfo.mSharedCount = object.GetValue<int>("sharedCount");

    const auto groups = aos::common::utils::GetArrayValue<std::string>(object, "groups");

    for (const auto& group : groups) {
        AOS_ERROR_CHECK_AND_THROW(
            "parsed groups count exceeds application limit", deviceInfo.mGroups.PushBack(group.c_str()));
    }

    const auto hostDevices = aos::common::utils::GetArrayValue<std::string>(object, "hostDevices");

    for (const auto& device : hostDevices) {
        AOS_ERROR_CHECK_AND_THROW(
            "parsed host devices count exceeds application limit", deviceInfo.mHostDevices.PushBack(device.c_str()));
    }

    return deviceInfo;
}

void DevicesFromJSON(
    const aos::common::utils::CaseInsensitiveObjectWrapper& object, aos::Array<aos::DeviceInfo>& outDevices)
{
    const auto devices = aos::common::utils::GetArrayValue<aos::DeviceInfo>(object, "devices",
        [](const auto& value) { return DeviceInfoFromJSON(aos::common::utils::CaseInsensitiveObjectWrapper(value)); });

    for (const auto& device : devices) {
        AOS_ERROR_CHECK_AND_THROW("parsed devices count exceeds application limit", outDevices.PushBack(device));
    }
}

aos::FileSystemMount FileSystemMountFromJSON(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    aos::FileSystemMount fsMount;

    const auto destination = object.GetValue<std::string>("destination");
    const auto type        = object.GetValue<std::string>("type");
    const auto source      = object.GetValue<std::string>("source");

    fsMount.mDestination = destination.c_str();
    fsMount.mType        = type.c_str();
    fsMount.mSource      = source.c_str();

    const auto options = aos::common::utils::GetArrayValue<std::string>(object, "options");

    for (const auto& option : options) {
        auto err = fsMount.mOptions.PushBack(option.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed options count exceeds application limit", err);
    }

    return fsMount;
}

aos::Host HostFromJSON(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return {object.GetValue<std::string>("ip").c_str(), object.GetValue<std::string>("hostName").c_str()};
}

aos::ResourceInfo ResourceInfoFromJSON(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    aos::ResourceInfo resourceInfo;

    const auto name = object.GetValue<std::string>("name");

    resourceInfo.mName = name.c_str();

    const auto groups = aos::common::utils::GetArrayValue<std::string>(object, "groups");

    for (const auto& group : groups) {
        auto err = resourceInfo.mGroups.PushBack(group.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed groups count exceeds application limit", err);
    }

    const auto mounts
        = aos::common::utils::GetArrayValue<aos::FileSystemMount>(object, "mounts", [](const auto& value) {
              return FileSystemMountFromJSON(aos::common::utils::CaseInsensitiveObjectWrapper(value));
          });

    for (const auto& mount : mounts) {
        auto err = resourceInfo.mMounts.PushBack(mount);
        AOS_ERROR_CHECK_AND_THROW("parsed mounts count exceeds application limit", err);
    }

    const auto envs = aos::common::utils::GetArrayValue<std::string>(object, "env");

    for (const auto& env : envs) {
        auto err = resourceInfo.mEnv.PushBack(env.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed envs count exceeds application limit", err);
    }

    const auto hosts = aos::common::utils::GetArrayValue<aos::Host>(object, "hosts",
        [](const auto& value) { return HostFromJSON(aos::common::utils::CaseInsensitiveObjectWrapper(value)); });

    for (const auto& host : hosts) {
        auto err = resourceInfo.mHosts.PushBack(host);
        AOS_ERROR_CHECK_AND_THROW("parsed hosts count exceeds application limit", err);
    }

    return resourceInfo;
}

void ResourcesFromJSON(
    const aos::common::utils::CaseInsensitiveObjectWrapper& object, aos::Array<aos::ResourceInfo>& outResources)
{
    const auto resources
        = aos::common::utils::GetArrayValue<aos::ResourceInfo>(object, "resources", [](const auto& value) {
              return ResourceInfoFromJSON(aos::common::utils::CaseInsensitiveObjectWrapper(value));
          });

    for (const auto& resource : resources) {
        auto err = outResources.PushBack(resource);
        AOS_ERROR_CHECK_AND_THROW("parsed resources count exceeds application limit", err);
    }
}

void LabelsFromJSON(const aos::common::utils::CaseInsensitiveObjectWrapper& object,
    aos::Array<aos::StaticString<aos::cLabelNameLen>>&                      outLabels)
{
    const auto labels = aos::common::utils::GetArrayValue<std::string>(object, "labels");

    for (const auto& label : labels) {
        auto err = outLabels.PushBack(label.c_str());
        AOS_ERROR_CHECK_AND_THROW("parsed labels count exceeds application limit", err);
    }
}

template <size_t cMaxSize>
Poco::JSON::Array ToJSONArray(const aos::Array<aos::StaticString<cMaxSize>>& aosArray)
{
    Poco::JSON::Array array;

    for (const auto& group : aosArray) {
        array.add(group.CStr());
    }

    return array;
}

Poco::JSON::Array DevicesToJson(const aos::Array<aos::DeviceInfo>& devices)
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

Poco::JSON::Array MountsToJson(const aos::Array<aos::FileSystemMount>& mounts)
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

Poco::JSON::Array HostsToJson(const aos::Array<aos::Host>& hosts)
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

Poco::JSON::Array ResourcesToJson(const aos::Array<aos::ResourceInfo>& resources)
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

} // namespace

namespace aos::common::jsonprovider {

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

        json = utils::Stringify(object).c_str();
    } catch (const Poco::Exception& e) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, e.what()));
    }

    return ErrorEnum::eNone;
}

Error JSONProvider::NodeConfigFromJSON(const String& json, sm::resourcemanager::NodeConfig& nodeConfig) const
{
    try {
        Poco::JSON::Parser                  parser;
        auto                                result = parser.parse(json.CStr());
        utils::CaseInsensitiveObjectWrapper object(result.extract<Poco::JSON::Object::Ptr>());

        const auto version  = object.GetValue<std::string>("version");
        const auto nodeType = object.GetValue<std::string>("nodeType");

        nodeConfig.mVersion              = version.c_str();
        nodeConfig.mNodeConfig.mNodeType = nodeType.c_str();
        nodeConfig.mNodeConfig.mPriority = object.GetValue<uint32_t>("priority");

        DevicesFromJSON(object, nodeConfig.mNodeConfig.mDevices);
        ResourcesFromJSON(object, nodeConfig.mNodeConfig.mResources);
        LabelsFromJSON(object, nodeConfig.mNodeConfig.mLabels);
    } catch (const utils::AosException& e) {
        return Error(e.GetError(), e.message().c_str());
    } catch (const Poco::Exception& e) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, e.what()));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::jsonprovider
