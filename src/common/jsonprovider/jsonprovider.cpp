/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "jsonprovider.hpp"

namespace aos::common::jsonprovider {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

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

aos::ResourceRatios ResourceRatiosFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    aos::ResourceRatios ratios = {};

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

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error JSONProvider::NodeConfigToJSON(const NodeConfig& nodeConfig, String& json) const
{
    try {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("version", nodeConfig.mVersion.CStr());
        object->set("nodeType", nodeConfig.mNodeType.CStr());
        object->set("nodeId", nodeConfig.mNodeID.CStr());

        if (nodeConfig.mAlertRules.HasValue()) {
            object->set("alertRules", AlertRulesToJSON(*nodeConfig.mAlertRules));
        }

        if (nodeConfig.mResourceRatios.HasValue()) {
            object->set("resourceRatios", ResourceRatiosToJSON(*nodeConfig.mResourceRatios));
        }

        object->set("labels", utils::ToJsonArray(nodeConfig.mLabels, utils::ToStdString));
        object->set("priority", nodeConfig.mPriority);

        json = utils::Stringify(object).c_str();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error JSONProvider::NodeConfigFromJSON(const String& json, NodeConfig& nodeConfig) const
{
    try {
        Poco::JSON::Parser                  parser;
        auto                                result = parser.parse(json.CStr());
        utils::CaseInsensitiveObjectWrapper object(result.extract<Poco::JSON::Object::Ptr>());

        auto err = nodeConfig.mVersion.Assign(object.GetValue<std::string>("version").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed version length exceeds application limit");

        err = nodeConfig.mNodeType.Assign(object.GetValue<std::string>("nodeType").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed node type length exceeds application limit");

        err = nodeConfig.mNodeID.Assign(object.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed node ID length exceeds application limit");

        if (object.Has("alertRules")) {
            nodeConfig.mAlertRules.SetValue(AlertRulesFromJSON(object.GetObject("alertRules")));
        }

        if (object.Has("resourceRatios")) {
            nodeConfig.mResourceRatios.SetValue(ResourceRatiosFromJSON(object.GetObject("resourceRatios")));
        }

        LabelsFromJSON(object, nodeConfig.mLabels);

        nodeConfig.mPriority = object.GetValue<uint32_t>("priority");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::jsonprovider
