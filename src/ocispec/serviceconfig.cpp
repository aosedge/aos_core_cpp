/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include "ocispec/ocispec.hpp"

#include "utils/exception.hpp"
#include "utils/json.hpp"
#include "utils/time.hpp"

namespace aos::common::oci {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

std::string ToStdString(const String& str)
{
    return str.CStr();
}

void RunParametersFromJSON(const utils::CaseInsensitiveObjectWrapper& object, RunParameters& params)
{
    params.mStartBurst = object.GetValue<long>("startBurst");

    Error           err;
    utils::Duration duration;

    if (const auto startInterval = object.GetOptionalValue<std::string>("startInterval"); startInterval.has_value()) {
        Tie(duration, err) = utils::ParseDuration(*startInterval);
        AOS_ERROR_CHECK_AND_THROW("start interval parsing error", err);

        params.mStartInterval = duration.count();
    }

    if (const auto restartInterval = object.GetOptionalValue<std::string>("restartInterval");
        restartInterval.has_value()) {
        Tie(duration, err) = utils::ParseDuration(*restartInterval);
        AOS_ERROR_CHECK_AND_THROW("restart interval parsing error", err);

        params.mRestartInterval = duration.count();
    }
}

Poco::JSON::Object RunParametersToJSON(const RunParameters& params)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    Error       err;
    std::string durationStr;

    if (params.mStartInterval > 0) {
        Tie(durationStr, err) = utils::FormatISO8601Duration(utils::Duration(params.mStartInterval));
        AOS_ERROR_CHECK_AND_THROW("start interval formatting error", err);

        object.set("startInterval", durationStr);
    }

    if (params.mStartBurst > 0) {
        object.set("startBurst", params.mStartBurst);
    }

    if (params.mRestartInterval > 0) {
        Tie(durationStr, err) = utils::FormatISO8601Duration(utils::Duration(params.mRestartInterval));
        AOS_ERROR_CHECK_AND_THROW("restart interval formatting error", err);

        object.set("restartInterval", durationStr);
    }

    return object;
}

void SysctlFromJSON(const Poco::Dynamic::Var& var, decltype(aos::oci::ServiceConfig::mSysctl)& sysctl)
{
    auto object = var.extract<Poco::JSON::Object::Ptr>();

    for (const auto& [key, value] : *object) {
        const auto valueStr = value.convert<std::string>();

        auto err = sysctl.TryEmplace(key.c_str(), valueStr.c_str());
        AOS_ERROR_CHECK_AND_THROW("sysctl parsing error", err);
    }
}

Poco::JSON::Object SysctlToJSON(const decltype(aos::oci::ServiceConfig::mSysctl)& sysctl)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    for (const auto& [key, value] : sysctl) {
        object.set(key.CStr(), value.CStr());
    }

    return object;
}

void ServiceQuotasFromJSON(const utils::CaseInsensitiveObjectWrapper& object, aos::oci::ServiceQuotas& quotas)
{
    if (const auto cpuDMIPSLimit = object.GetOptionalValue<uint64_t>("cpuDMIPSLimit")) {
        quotas.mCPUDMIPSLimit.SetValue(*cpuDMIPSLimit);
    }

    if (const auto ramLimit = object.GetOptionalValue<uint64_t>("ramLimit")) {
        quotas.mRAMLimit.SetValue(*ramLimit);
    }

    if (const auto pidsLimit = object.GetOptionalValue<uint64_t>("pidsLimit")) {
        quotas.mPIDsLimit.SetValue(*pidsLimit);
    }

    if (const auto noFileLimit = object.GetOptionalValue<uint64_t>("noFileLimit")) {
        quotas.mNoFileLimit.SetValue(*noFileLimit);
    }

    if (const auto tmpLimit = object.GetOptionalValue<uint64_t>("tmpLimit")) {
        quotas.mTmpLimit.SetValue(*tmpLimit);
    }

    if (const auto stateLimit = object.GetOptionalValue<uint64_t>("stateLimit")) {
        quotas.mStateLimit.SetValue(*stateLimit);
    }

    if (const auto storageLimit = object.GetOptionalValue<uint64_t>("storageLimit")) {
        quotas.mStorageLimit.SetValue(*storageLimit);
    }

    if (const auto uploadSpeed = object.GetOptionalValue<uint64_t>("uploadSpeed")) {
        quotas.mUploadSpeed.SetValue(*uploadSpeed);
    }

    if (const auto downloadSpeed = object.GetOptionalValue<uint64_t>("downloadSpeed")) {
        quotas.mDownloadSpeed.SetValue(*downloadSpeed);
    }

    if (const auto uploadLimit = object.GetOptionalValue<uint64_t>("uploadLimit")) {
        quotas.mUploadLimit.SetValue(*uploadLimit);
    }

    if (const auto downloadLimit = object.GetOptionalValue<uint64_t>("downloadLimit")) {
        quotas.mDownloadLimit.SetValue(*downloadLimit);
    }
}

Poco::JSON::Object ServiceQuotasToJSON(const aos::oci::ServiceQuotas& quotas)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    if (quotas.mCPUDMIPSLimit.HasValue()) {
        object.set("cpuDMIPSLimit", quotas.mCPUDMIPSLimit.GetValue());
    }

    if (quotas.mRAMLimit.HasValue()) {
        object.set("ramLimit", quotas.mRAMLimit.GetValue());
    }

    if (quotas.mPIDsLimit.HasValue()) {
        object.set("pidsLimit", quotas.mPIDsLimit.GetValue());
    }

    if (quotas.mNoFileLimit.HasValue()) {
        object.set("noFileLimit", quotas.mNoFileLimit.GetValue());
    }

    if (quotas.mTmpLimit.HasValue()) {
        object.set("tmpLimit", quotas.mTmpLimit.GetValue());
    }

    if (quotas.mStateLimit.HasValue()) {
        object.set("stateLimit", quotas.mStateLimit.GetValue());
    }

    if (quotas.mStorageLimit.HasValue()) {
        object.set("storageLimit", quotas.mStorageLimit.GetValue());
    }

    if (quotas.mUploadSpeed.HasValue()) {
        object.set("uploadSpeed", quotas.mUploadSpeed.GetValue());
    }

    if (quotas.mDownloadSpeed.HasValue()) {
        object.set("downloadSpeed", quotas.mDownloadSpeed.GetValue());
    }

    if (quotas.mUploadLimit.HasValue()) {
        object.set("uploadLimit", quotas.mUploadLimit.GetValue());
    }

    if (quotas.mDownloadLimit.HasValue()) {
        object.set("downloadLimit", quotas.mDownloadLimit.GetValue());
    }

    return object;
}

aos::oci::RequestedResources RequestedResourcesFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    aos::oci::RequestedResources resources = {};

    if (const auto cpu = object.GetOptionalValue<uint64_t>("cpu")) {
        resources.mCPU.SetValue(*cpu);
    }

    if (const auto ram = object.GetOptionalValue<uint64_t>("ram")) {
        resources.mRAM.SetValue(*ram);
    }

    if (const auto storage = object.GetOptionalValue<uint64_t>("storage")) {
        resources.mStorage.SetValue(*storage);
    }

    if (const auto state = object.GetOptionalValue<uint64_t>("state")) {
        resources.mState.SetValue(*state);
    }

    return resources;
}

Poco::JSON::Object RequestedResourcesToJSON(const aos::oci::RequestedResources& resources)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    if (resources.mCPU.HasValue()) {
        object.set("cpu", resources.mCPU.GetValue());
    }

    if (resources.mRAM.HasValue()) {
        object.set("ram", resources.mRAM.GetValue());
    }

    if (resources.mStorage.HasValue()) {
        object.set("storage", resources.mStorage.GetValue());
    }

    if (resources.mState.HasValue()) {
        object.set("state", resources.mState.GetValue());
    }

    return object;
}

aos::oci::ServiceDevice ServiceDeviceFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    const auto device      = object.GetValue<std::string>("device");
    const auto permissions = object.GetValue<std::string>("permissions");

    return {device.c_str(), permissions.c_str()};
}

Poco::JSON::Object ServiceDeviceToJSON(const aos::oci::ServiceDevice& device)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    object.set("device", device.mDevice.CStr());
    object.set("permissions", device.mPermissions.CStr());

    return object;
}

FunctionPermissions FunctionPermissionsFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    const auto function    = object.GetValue<std::string>("function");
    const auto permissions = object.GetValue<std::string>("permissions");

    return {function.c_str(), permissions.c_str()};
}

Poco::JSON::Object FunctionPermissionsToJSON(const FunctionPermissions& permissions)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    object.set("function", permissions.mFunction.CStr());
    object.set("permissions", permissions.mPermissions.CStr());

    return object;
}

FunctionServicePermissions FunctionServicePermissionsFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    const auto name        = object.GetValue<std::string>("name");
    const auto permissions = utils::GetArrayValue<FunctionPermissions>(object, "permissions",
        [](const auto& value) { return FunctionPermissionsFromJSON(utils::CaseInsensitiveObjectWrapper(value)); });

    FunctionServicePermissions functionServicePermissions;

    functionServicePermissions.mName = name.c_str();

    for (const auto& permission : permissions) {
        auto err = functionServicePermissions.mPermissions.PushBack(permission);
        AOS_ERROR_CHECK_AND_THROW("function permissions parsing error", err);
    }

    return functionServicePermissions;
}

Poco::JSON::Object FunctionServicePermissionsToJSON(const FunctionServicePermissions& permissions)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    object.set("name", permissions.mName.CStr());
    object.set("permissions", utils::ToJsonArray(permissions.mPermissions, FunctionPermissionsToJSON));

    return object;
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
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

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
    auto object = AlertRuleToJSON<AlertRulePercents>(rule);

    object.set("name", rule.mName.CStr());

    return object;
}

Poco::JSON::Object AlertRulesToJSON(const AlertRules& rules)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

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

Error OCISpec::LoadServiceConfig(const String& path, aos::oci::ServiceConfig& serviceConfig)
{
    try {
        std::ifstream file(path.CStr());

        if (!file.is_open()) {
            AOS_ERROR_THROW("failed to open file", ErrorEnum::eNotFound);
        }

        auto [var, err] = utils::ParseJson(file);
        AOS_ERROR_CHECK_AND_THROW("failed to parse json", err);

        Poco::JSON::Object::Ptr             object = var.extract<Poco::JSON::Object::Ptr>();
        utils::CaseInsensitiveObjectWrapper wrapper(object);

        if (const auto created = wrapper.GetOptionalValue<std::string>("created"); created.has_value()) {
            Tie(serviceConfig.mCreated, err) = utils::FromUTCString(created->c_str());
            AOS_ERROR_CHECK_AND_THROW("created time parsing error", err);
        }

        const auto author     = wrapper.GetValue<std::string>("author");
        serviceConfig.mAuthor = author.c_str();

        serviceConfig.mSkipResourceLimits = wrapper.GetValue<bool>("skipResourceLimits");

        if (wrapper.Has("hostname")) {
            const auto hostname = wrapper.GetValue<std::string>("hostname");
            serviceConfig.mHostname.SetValue(hostname.c_str());
        }

        const auto balancingPolicy     = wrapper.GetValue<std::string>("balancingPolicy");
        serviceConfig.mBalancingPolicy = balancingPolicy.c_str();

        const auto runners = utils::GetArrayValue<std::string>(wrapper, "runners");
        for (const auto& runner : runners) {
            err = serviceConfig.mRunners.PushBack(runner.c_str());
            AOS_ERROR_CHECK_AND_THROW("runners parsing error", err);
        }

        if (wrapper.Has("runParameters")) {
            RunParametersFromJSON(wrapper.GetObject("runParameters"), serviceConfig.mRunParameters);
        }

        if (wrapper.Has("sysctl")) {
            SysctlFromJSON(wrapper.Get("sysctl"), serviceConfig.mSysctl);
        }

        if (const auto offlineTTLStr = wrapper.GetOptionalValue<std::string>("offlineTTL"); offlineTTLStr.has_value()) {
            utils::Duration offlineTTL;

            Tie(offlineTTL, err) = utils::ParseDuration(*offlineTTLStr);
            AOS_ERROR_CHECK_AND_THROW("offlineTTL parsing error", err);

            serviceConfig.mOfflineTTL = offlineTTL.count();
        }

        if (wrapper.Has("quotas")) {
            ServiceQuotasFromJSON(wrapper.GetObject("quotas"), serviceConfig.mQuotas);
        }

        if (wrapper.Has("requestedResources")) {
            serviceConfig.mRequestedResources.SetValue(
                RequestedResourcesFromJSON(wrapper.GetObject("requestedResources")));
        }

        const auto devices = utils::GetArrayValue<aos::oci::ServiceDevice>(wrapper, "devices",
            [](const auto& value) { return ServiceDeviceFromJSON(utils::CaseInsensitiveObjectWrapper(value)); });

        for (const auto& device : devices) {
            err = serviceConfig.mDevices.PushBack(device);
            AOS_ERROR_CHECK_AND_THROW("devices parsing error", err);
        }

        for (const auto& resource : utils::GetArrayValue<std::string>(wrapper, "resources")) {
            err = serviceConfig.mResources.PushBack(resource.c_str());
            AOS_ERROR_CHECK_AND_THROW("resources parsing error", err);
        }

        const auto permissions
            = utils::GetArrayValue<FunctionServicePermissions>(wrapper, "permissions", [](const auto& value) {
                  return FunctionServicePermissionsFromJSON(utils::CaseInsensitiveObjectWrapper(value));
              });

        for (const auto& permission : permissions) {
            err = serviceConfig.mPermissions.PushBack(permission);
            AOS_ERROR_CHECK_AND_THROW("permissions parsing error", err);
        }

        if (wrapper.Has("alertRules")) {
            serviceConfig.mAlertRules.SetValue(AlertRulesFromJSON(wrapper.GetObject("alertRules")));
        }

    } catch (const utils::AosException& e) {
        return AOS_ERROR_WRAP(Error(e.GetError(), e.message().c_str()));
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, e.what()));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveServiceConfig(const String& path, const aos::oci::ServiceConfig& serviceConfig)
{
    try {
        Poco::JSON::Object::Ptr object = new Poco::JSON::Object(Poco::JSON_PRESERVE_KEY_ORDER);

        auto [created, err] = utils::ToUTCString(serviceConfig.mCreated);
        AOS_ERROR_CHECK_AND_THROW("created time parsing error", err);

        object->set("created", created);
        object->set("author", serviceConfig.mAuthor.CStr());
        object->set("skipResourceLimits", serviceConfig.mSkipResourceLimits);

        if (serviceConfig.mHostname.HasValue() && !serviceConfig.mHostname->IsEmpty()) {
            object->set("hostname", serviceConfig.mHostname->CStr());
        }

        object->set("balancingPolicy", serviceConfig.mBalancingPolicy.CStr());
        object->set("runners", utils::ToJsonArray(serviceConfig.mRunners, ToStdString));

        if (auto runParametersObject = RunParametersToJSON(serviceConfig.mRunParameters);
            runParametersObject.size() > 0) {
            object->set("runParameters", runParametersObject);
        }

        if (!serviceConfig.mSysctl.IsEmpty()) {
            object->set("sysctl", SysctlToJSON(serviceConfig.mSysctl));
        }

        if (serviceConfig.mOfflineTTL > 0) {
            std::string offlineTTLStr;

            Tie(offlineTTLStr, err) = utils::FormatISO8601Duration(utils::Duration(serviceConfig.mOfflineTTL));
            AOS_ERROR_CHECK_AND_THROW("offlineTTL formatting error", err);

            object->set("offlineTTL", offlineTTLStr);
        }

        object->set("quotas", ServiceQuotasToJSON(serviceConfig.mQuotas));

        if (serviceConfig.mRequestedResources.HasValue()) {
            object->set("requestedResources", RequestedResourcesToJSON(serviceConfig.mRequestedResources.GetValue()));
        }

        if (!serviceConfig.mDevices.IsEmpty()) {
            object->set("devices", utils::ToJsonArray(serviceConfig.mDevices, ServiceDeviceToJSON));
        }

        if (!serviceConfig.mResources.IsEmpty()) {
            object->set("resources", utils::ToJsonArray(serviceConfig.mResources, ToStdString));
        }

        if (!serviceConfig.mPermissions.IsEmpty()) {
            object->set(
                "permissions", utils::ToJsonArray(serviceConfig.mPermissions, FunctionServicePermissionsToJSON));
        }

        if (serviceConfig.mAlertRules.HasValue()) {
            object->set("alertRules", AlertRulesToJSON(serviceConfig.mAlertRules.GetValue()));
        }

        err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW("failed to write json to file", err);
    } catch (const utils::AosException& e) {
        return AOS_ERROR_WRAP(Error(e.GetError(), e.message().c_str()));
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, e.what()));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::oci
