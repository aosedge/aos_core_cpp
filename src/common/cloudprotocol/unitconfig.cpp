/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>
#include <core/common/tools/logger.hpp>

#include "common.hpp"
#include "unitconfig.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

AlertRulePercents AlertRulePercentsFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    AlertRulePercents percents = {};

    if (const auto minTimeout = object.GetOptionalValue<std::string>("minTimeout"); minTimeout.has_value()) {
        Error err = {};

        Tie(percents.mMinTimeout, err) = common::utils::ParseDuration(minTimeout->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse minTimeout");
    }

    percents.mMinThreshold = object.GetValue<double>("minThreshold");
    percents.mMaxThreshold = object.GetValue<double>("maxThreshold");

    return percents;
}

AlertRulePoints AlertRulePointsFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    AlertRulePoints points = {};

    if (const auto minTimeout = object.GetOptionalValue<std::string>("minTimeout"); minTimeout.has_value()) {
        Error err = {};

        Tie(points.mMinTimeout, err) = common::utils::ParseDuration(minTimeout->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse minTimeout");
    }

    points.mMinThreshold = object.GetValue<uint64_t>("minThreshold");
    points.mMaxThreshold = object.GetValue<uint64_t>("maxThreshold");

    return points;
}

PartitionAlertRule PartitionAlertRuleFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    const auto name = object.GetValue<std::string>("name");

    return {AlertRulePercentsFromJSON(object), name.c_str()};
}

AlertRules AlertRulesFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    AlertRules rules = {};

    if (object.Has("ram")) {
        rules.mRAM.SetValue(AlertRulePercentsFromJSON(object.GetObject("ram")));
    }

    if (object.Has("cpu")) {
        rules.mCPU.SetValue(AlertRulePercentsFromJSON(object.GetObject("cpu")));
    }

    if (object.Has("partitions")) {
        auto partitions = common::utils::GetArrayValue<PartitionAlertRule>(object, "partitions", [](const auto& value) {
            return PartitionAlertRuleFromJSON(common::utils::CaseInsensitiveObjectWrapper(value));
        });

        for (const auto& partition : partitions) {
            auto err = rules.mPartitions.PushBack(partition);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse partition");
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

ResourceRatios ResourceRatiosFromJSON(const common::utils::CaseInsensitiveObjectWrapper& object)
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

Poco::JSON::Object::Ptr ResourceRatiosToJSON(const aos::ResourceRatios& ratios)
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

} // namespace

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

Error ToJSON(const NodeConfig& nodeConfig, Poco::JSON::Object& json)
{
    if (!nodeConfig.mVersion.IsEmpty()) {
        json.set("version", nodeConfig.mVersion.CStr());
    }

    AosIdentity identity;

    identity.mCodename = nodeConfig.mNodeID.CStr();
    json.set("node", CreateAosIdentity(identity));

    identity.mCodename = nodeConfig.mNodeType.CStr();
    json.set("nodeGroupSubject", CreateAosIdentity(identity));

    if (nodeConfig.mAlertRules.HasValue()) {
        json.set("alertRules", AlertRulesToJSON(*nodeConfig.mAlertRules));
    }

    if (nodeConfig.mResourceRatios.HasValue()) {
        json.set("resourceRatios", ResourceRatiosToJSON(*nodeConfig.mResourceRatios));
    }

    json.set("labels", utils::ToJsonArray(nodeConfig.mLabels, utils::ToStdString));
    json.set("priority", nodeConfig.mPriority);

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, NodeConfig& nodeConfig)
{
    try {
        if (json.Has("version")) {
            auto err = nodeConfig.mVersion.Assign(json.GetValue<std::string>("version").c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse version");
        }

        {
            AosIdentity identity;

            auto err = ParseAosIdentity(json.GetObject("nodeGroupSubject"), identity);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse nodeGroupSubject");

            if (!identity.mCodename.has_value()) {
                AOS_ERROR_THROW(ErrorEnum::eNotFound, "nodeGroupSubject codename is missing");
            }

            err = nodeConfig.mNodeType.Assign(identity.mCodename->c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse codename");
        }

        {
            AosIdentity identity;

            auto err = ParseAosIdentity(json.GetObject("node"), identity);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

            if (!identity.mCodename.has_value()) {
                AOS_ERROR_THROW(ErrorEnum::eNotFound, "node codename is missing");
            }

            err = nodeConfig.mNodeID.Assign(identity.mCodename->c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse nodeID");
        }

        if (json.Has("alertRules")) {
            nodeConfig.mAlertRules.EmplaceValue(AlertRulesFromJSON(json.GetObject("alertRules")));
        }

        if (json.Has("resourceRatios")) {
            nodeConfig.mResourceRatios.EmplaceValue(ResourceRatiosFromJSON(json.GetObject("resourceRatios")));
        }

        if (json.Has("labels")) {
            auto err = LabelsFromJSON(json, nodeConfig.mLabels);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse labels");
        }

        nodeConfig.mPriority = json.GetValue<uint64_t>("priority");

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }
}

Error ToJSON(const UnitConfig& unitConfig, Poco::JSON::Object& json)
{
    try {
        json.set("version", unitConfig.mVersion.CStr());
        json.set("formatVersion", unitConfig.mFormatVersion.CStr());
        json.set("nodes", common::utils::ToJsonArray(unitConfig.mNodes, [](const auto& nodeConfig) {
            Poco::JSON::Object nodeJson(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(nodeConfig, nodeJson);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert nodeConfig to JSON");

            return nodeJson;
        }));

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, UnitConfig& unitConfig)
{
    try {
        auto err = unitConfig.mVersion.Assign(json.GetValue<std::string>("version").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed version length exceeds application limit");

        err = unitConfig.mFormatVersion.Assign(json.GetValue<std::string>("formatVersion").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed format version length exceeds application limit");

        common::utils::ForEach(json, "nodes", [&unitConfig](const auto& value) {
            auto err = unitConfig.mNodes.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't create node config");

            err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(value), unitConfig.mNodes.Back());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse node config");
        });

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

} // namespace aos::common::cloudprotocol
