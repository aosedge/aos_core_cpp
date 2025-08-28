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
#include "desiredstatus.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

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

aos::cloudprotocol::ResourceRatios ResourceRatiosFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    aos::cloudprotocol::ResourceRatios ratios = {};

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

Poco::JSON::Object::Ptr ResourceRatiosToJSON(const aos::cloudprotocol::ResourceRatios& ratios)
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
        if (json.Has("node")) {
            nodeConfig.mNode.EmplaceValue();

            auto err = FromJSON(json.GetObject("node"), *nodeConfig.mNode);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse node identifier from JSON");
        }

        auto err = FromJSON(json.GetObject("nodeGroupSubject"), nodeConfig.mNodeGroupSubject);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse node group subject from JSON");

        if (json.Has("alertRules")) {
            nodeConfig.mAlertRules.SetValue(AlertRulesFromJSON(json.GetObject("alertRules")));
        }

        if (json.Has("resourceRatios")) {
            nodeConfig.mResourceRatios.SetValue(ResourceRatiosFromJSON(json.GetObject("resourceRatios")));
        }

        LabelsFromJSON(json, nodeConfig.mLabels);
        nodeConfig.mPriority = json.GetValue<uint32_t>("priority");

    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::NodeConfig& nodeConfig, Poco::JSON::Object& json)
{
    try {
        auto node = Poco::makeShared<Poco::JSON::Object>();

        if (nodeConfig.mNode.HasValue()) {
            auto err = ToJSON(*nodeConfig.mNode, *node);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert node identifier to JSON");
        }

        json.set("node", node);

        auto nodeGroupSubject = Poco::makeShared<Poco::JSON::Object>();

        auto err = ToJSON(nodeConfig.mNodeGroupSubject, *nodeGroupSubject);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert node group subject to JSON");

        json.set("nodeGroupSubject", nodeGroupSubject);

        if (nodeConfig.mAlertRules.HasValue()) {
            json.set("alertRules", AlertRulesToJSON(*nodeConfig.mAlertRules));
        }

        if (nodeConfig.mResourceRatios.HasValue()) {
            json.set("resourceRatios", ResourceRatiosToJSON(*nodeConfig.mResourceRatios));
        }

        json.set("labels", utils::ToJsonArray(nodeConfig.mLabels, ToStdString));
        json.set("priority", nodeConfig.mPriority);
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
