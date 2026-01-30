/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <sstream>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "ocispec.hpp"

namespace aos::common::oci {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void RunParametersFromJSON(const utils::CaseInsensitiveObjectWrapper& object, RunParameters& params)
{
    if (const auto startBurst = object.GetOptionalValue<long>("startBurst")) {
        params.mStartBurst.SetValue(*startBurst);
    }

    Error err;

    if (const auto startInterval = object.GetOptionalValue<std::string>("startInterval"); startInterval.has_value()) {
        // cppcheck-suppress unusedScopedObject
        Tie(params.mStartInterval, err) = utils::ParseDuration(*startInterval);
        AOS_ERROR_CHECK_AND_THROW(err, "start interval parsing error");
    }

    if (const auto restartInterval = object.GetOptionalValue<std::string>("restartInterval");
        restartInterval.has_value()) {
        // cppcheck-suppress unusedScopedObject
        Tie(params.mRestartInterval, err) = utils::ParseDuration(*restartInterval);
        AOS_ERROR_CHECK_AND_THROW(err, "restart interval parsing error");
    }
}

Poco::JSON::Object RunParametersToJSON(const RunParameters& params)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    if (params.mStartInterval.HasValue()) {
        auto durationStr = params.mStartInterval->ToISO8601String();
        object.set("startInterval", durationStr.CStr());
    }

    if (params.mStartBurst.HasValue()) {
        object.set("startBurst", *params.mStartBurst);
    }

    if (params.mRestartInterval.HasValue()) {
        auto durationStr = params.mRestartInterval->ToISO8601String();
        object.set("restartInterval", durationStr.CStr());
    }

    return object;
}

void SysctlFromJSON(const Poco::Dynamic::Var& var, decltype(aos::oci::ItemConfig::mSysctl)& sysctl)
{
    auto object = var.extract<Poco::JSON::Object::Ptr>();

    for (const auto& [key, value] : *object) {
        const auto valueStr = value.convert<std::string>();

        auto err = sysctl.TryEmplace(key.c_str(), valueStr.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "sysctl parsing error");
    }
}

Poco::JSON::Object SysctlToJSON(const decltype(aos::oci::ItemConfig::mSysctl)& sysctl)
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

void FunctionServicePermissionsFromJSON(
    const utils::CaseInsensitiveObjectWrapper& object, FunctionServicePermissions& functionServicePermissions)
{
    const auto name        = object.GetValue<std::string>("name");
    const auto permissions = utils::GetArrayValue<FunctionPermissions>(object, "permissions",
        [](const auto& value) { return FunctionPermissionsFromJSON(utils::CaseInsensitiveObjectWrapper(value)); });

    functionServicePermissions.mName = name.c_str();

    for (const auto& permission : permissions) {
        auto err = functionServicePermissions.mPermissions.PushBack(permission);
        AOS_ERROR_CHECK_AND_THROW(err, "function permissions parsing error");
    }
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
Poco::JSON::Object AlertRuleToJSON(const T& rule)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    if (rule.mMinTimeout > 0) {
        auto duration = rule.mMinTimeout.ToISO8601String();
        object.set("minTimeout", duration.CStr());
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

Error OCISpec::LoadItemConfig(const String& path, aos::oci::ItemConfig& itemConfig)
{
    try {
        std::ifstream file(path.CStr());

        if (!file.is_open()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "failed to open file");
        }

        auto [var, err] = utils::ParseJson(file);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse json");

        Poco::JSON::Object::Ptr             object = var.extract<Poco::JSON::Object::Ptr>();
        utils::CaseInsensitiveObjectWrapper wrapper(object);

        if (const auto created = wrapper.GetOptionalValue<std::string>("created")) {
            Tie(itemConfig.mCreated, err) = utils::FromUTCString(created->c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "created time parsing error");
        }

        const auto author  = wrapper.GetValue<std::string>("author");
        itemConfig.mAuthor = author.c_str();

        itemConfig.mSkipResourceLimits = wrapper.GetValue<bool>("skipResourceLimits");

        if (wrapper.Has("hostname")) {
            const auto hostname = wrapper.GetValue<std::string>("hostname");
            itemConfig.mHostname.SetValue(hostname.c_str());
        }

        if (auto balancingPolicy = wrapper.GetOptionalValue<std::string>("balancingPolicy")) {
            err = itemConfig.mBalancingPolicy.FromString(balancingPolicy->c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "balancing policy parsing error");
        }

        const auto runtimes = utils::GetArrayValue<std::string>(wrapper, "runtimes");
        for (const auto& runtime : runtimes) {
            err = itemConfig.mRuntimes.PushBack(runtime.c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "runtimes parsing error");
        }

        if (wrapper.Has("runParameters")) {
            RunParametersFromJSON(wrapper.GetObject("runParameters"), itemConfig.mRunParameters);
        }

        if (wrapper.Has("sysctl")) {
            SysctlFromJSON(wrapper.Get("sysctl"), itemConfig.mSysctl);
        }

        if (const auto offlineTTLStr = wrapper.GetOptionalValue<std::string>("offlineTTL")) {
            Tie(itemConfig.mOfflineTTL, err) = utils::ParseDuration(*offlineTTLStr);
            AOS_ERROR_CHECK_AND_THROW(err, "offlineTTL parsing error");
        }

        if (wrapper.Has("quotas")) {
            ServiceQuotasFromJSON(wrapper.GetObject("quotas"), itemConfig.mQuotas);
        }

        if (wrapper.Has("requestedResources")) {
            itemConfig.mRequestedResources.SetValue(
                RequestedResourcesFromJSON(wrapper.GetObject("requestedResources")));
        }

        if (wrapper.Has("allowedConnections")) {
            for (const auto& connection : wrapper.GetObject("allowedConnections").GetNames()) {
                err = itemConfig.mAllowedConnections.PushBack(connection.c_str());
                AOS_ERROR_CHECK_AND_THROW(err, "allowedConnections parsing error");
            }
        }

        for (const auto& resource : utils::GetArrayValue<std::string>(wrapper, "resources")) {
            err = itemConfig.mResources.PushBack(resource.c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "resources parsing error");
        }

        utils::ForEach(wrapper, "permissions", [&itemConfig](const auto& value) {
            auto err = itemConfig.mPermissions.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "permissions parsing error");

            return FunctionServicePermissionsFromJSON(
                utils::CaseInsensitiveObjectWrapper(value), itemConfig.mPermissions.Back());
        });

        if (wrapper.Has("alertRules")) {
            itemConfig.mAlertRules.SetValue(AlertRulesFromJSON(wrapper.GetObject("alertRules")));
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveItemConfig(const String& path, const aos::oci::ItemConfig& itemConfig)
{
    try {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto [created, err] = utils::ToUTCString(itemConfig.mCreated);
        AOS_ERROR_CHECK_AND_THROW(err, "created time parsing error");

        object->set("created", created);
        object->set("author", itemConfig.mAuthor.CStr());
        object->set("skipResourceLimits", itemConfig.mSkipResourceLimits);

        if (itemConfig.mHostname.HasValue() && !itemConfig.mHostname->IsEmpty()) {
            object->set("hostname", itemConfig.mHostname->CStr());
        }

        object->set("balancingPolicy", itemConfig.mBalancingPolicy.ToString().CStr());
        object->set("runtimes", utils::ToJsonArray(itemConfig.mRuntimes, utils::ToStdString));

        if (auto runParametersObject = RunParametersToJSON(itemConfig.mRunParameters); runParametersObject.size() > 0) {
            object->set("runParameters", runParametersObject);
        }

        if (!itemConfig.mSysctl.IsEmpty()) {
            object->set("sysctl", SysctlToJSON(itemConfig.mSysctl));
        }

        if (itemConfig.mOfflineTTL > 0) {
            auto offlineTTLStr = itemConfig.mOfflineTTL.ToISO8601String();
            object->set("offlineTTL", offlineTTLStr.CStr());
        }

        object->set("quotas", ServiceQuotasToJSON(itemConfig.mQuotas));

        if (itemConfig.mRequestedResources.HasValue()) {
            object->set("requestedResources", RequestedResourcesToJSON(itemConfig.mRequestedResources.GetValue()));
        }

        if (!itemConfig.mAllowedConnections.IsEmpty()) {
            Poco::JSON::Object allowedConnectionsObj {Poco::JSON_PRESERVE_KEY_ORDER};

            for (const auto& connection : itemConfig.mAllowedConnections) {
                allowedConnectionsObj.set(connection.CStr(), Poco::JSON::Object {});
            }

            object->set("allowedConnections", allowedConnectionsObj);
        }

        if (!itemConfig.mResources.IsEmpty()) {
            object->set("resources", utils::ToJsonArray(itemConfig.mResources, utils::ToStdString));
        }

        if (!itemConfig.mPermissions.IsEmpty()) {
            object->set("permissions", utils::ToJsonArray(itemConfig.mPermissions, FunctionServicePermissionsToJSON));
        }

        if (itemConfig.mAlertRules.HasValue()) {
            object->set("alertRules", AlertRulesToJSON(itemConfig.mAlertRules.GetValue()));
        }

        err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to write json to file");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::oci
