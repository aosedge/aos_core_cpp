/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Base64Decoder.h>

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "desiredstatus.hpp"

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

std::string Base64Decode(const std::string& encoded)
{
    std::istringstream  encodedStream(encoded);
    Poco::Base64Decoder decoder(encodedStream);

    return std::string(std::istreambuf_iterator<char>(decoder), std::istreambuf_iterator<char>());
}

void DesiredNodeStateInfoFromJson(const common::utils::CaseInsensitiveObjectWrapper& json, DesiredNodeStateInfo& node)
{
    AosIdentity identity;

    auto err = ParseAosIdentity(json.GetObject("item"), identity);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

    if (!identity.mCodename.has_value()) {
        AOS_ERROR_THROW(ErrorEnum::eNotFound, "item codename is missing");
    }

    err = node.mNodeID.Assign(identity.mCodename->c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse nodeID");

    err = node.mState.FromString(json.GetValue<std::string>("state").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse state");
}

void LabelsFromJSON(
    const common::utils::CaseInsensitiveObjectWrapper& object, Array<StaticString<cLabelNameLen>>& outLabels)
{
    const auto labels = common::utils::GetArrayValue<std::string>(object, "labels");

    for (const auto& label : labels) {
        auto err = outLabels.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse label");

        err = outLabels.Back().Assign(label.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse label");
    }
}

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

void NodeConfigFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, NodeConfig& nodeConfig)
{
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
        LabelsFromJSON(json, nodeConfig.mLabels);
    }

    nodeConfig.mPriority = json.GetValue<uint64_t>("priority");
}

void UnitConfigFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, UnitConfig& unitConfig)
{
    auto err = unitConfig.mVersion.Assign(json.GetValue<std::string>("version").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse version");

    err = unitConfig.mFormatVersion.Assign(json.GetValue<std::string>("formatVersion").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse formatVersion");

    common::utils::ForEach(json, "nodes", [&unitConfig](const auto& value) {
        auto err = unitConfig.mNodes.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

        NodeConfigFromJSON(common::utils::CaseInsensitiveObjectWrapper(value), unitConfig.mNodes.Back());
    });
}

void UpdateItemInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, UpdateItemInfo& updateItemInfo)
{
    {
        AosIdentity identity;

        auto err = ParseAosIdentity(json.GetObject("item"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

        if (!identity.mID.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "item id is missing");
        }

        err = updateItemInfo.mItemID.Assign(identity.mID->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse itemID");

        if (!identity.mType.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "item type is missing");
        }

        updateItemInfo.mType = *identity.mType;
    }

    auto err = updateItemInfo.mVersion.Assign(json.GetValue<std::string>("version").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse version");

    {
        AosIdentity identity;

        err = ParseAosIdentity(json.GetObject("owner"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse owner");

        if (!identity.mID.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "owner id is missing");
        }

        err = updateItemInfo.mOwnerID.Assign(identity.mID->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse ownerID");
    }

    err = updateItemInfo.mIndexDigest.Assign(json.GetValue<std::string>("indexDigest").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse indexDigest");
}

void DesiredInstanceInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, DesiredInstanceInfo& instance)
{
    {
        AosIdentity identity;

        auto err = ParseAosIdentity(json.GetObject("item"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

        if (!identity.mID.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "item id is missing");
        }

        err = instance.mItemID.Assign(identity.mID->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse itemID");
    }

    {
        AosIdentity identity;

        auto err = ParseAosIdentity(json.GetObject("subject"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject");

        if (!identity.mID.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "subject id is missing");
        }

        err = instance.mSubjectID.Assign(identity.mID->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse subjectID");
    }

    instance.mPriority     = json.GetValue<size_t>("priority");
    instance.mNumInstances = json.GetValue<size_t>("numInstances");

    if (json.Has("labels")) {
        LabelsFromJSON(json, instance.mLabels);
    }
}

void SubjectInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, SubjectInfo& subject)
{
    AosIdentity identity;

    auto err = ParseAosIdentity(json.GetObject("identity"), identity);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject identity");

    if (!identity.mID.has_value()) {
        AOS_ERROR_THROW(ErrorEnum::eNotFound, "subject ID is missing");
    }

    err = subject.mSubjectID.Assign(identity.mID->c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse subjectID");

    err = subject.mSubjectType.FromString(json.GetValue<std::string>("type").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject type");
}

void CertificateInfoFromJSON(
    const common::utils::CaseInsensitiveObjectWrapper& json, crypto::CertificateInfo& certificateInfo)
{
    const auto certificate = Base64Decode(json.GetValue<std::string>("certificate"));

    auto err = certificateInfo.mCertificate.Assign(String(certificate.c_str()).AsByteArray());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate");

    err = certificateInfo.mFingerprint.Assign(json.GetValue<std::string>("fingerprint").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate fingerprint");
}

void CertificateChainFromJSON(
    const common::utils::CaseInsensitiveObjectWrapper& json, crypto::CertificateChainInfo& certificateChain)
{
    auto err = certificateChain.mName.Assign(json.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate chain name");

    common::utils::ForEach(json, "fingerprints", [&certificateChain](const Poco::Dynamic::Var& value) {
        auto err = certificateChain.mFingerprints.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate chain fingerprint");

        err = certificateChain.mFingerprints.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate chain fingerprint");
    });
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, DesiredStatus& desiredStatus)
{
    try {
        if (auto err = FromJSON(json, static_cast<Protocol&>(desiredStatus)); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        common::utils::ForEach(json, "nodes", [&desiredStatus](const auto& value) {
            auto err = desiredStatus.mNodes.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse nodes");

            DesiredNodeStateInfoFromJson(
                common::utils::CaseInsensitiveObjectWrapper(value), desiredStatus.mNodes.Back());
        });

        if (json.Has("unitConfig")) {
            desiredStatus.mUnitConfig.EmplaceValue();

            UnitConfigFromJSON(json.GetObject("unitConfig"), *desiredStatus.mUnitConfig);
        }

        common::utils::ForEach(json, "items", [&desiredStatus](const auto& value) {
            auto err = desiredStatus.mUpdateItems.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse items");

            UpdateItemInfoFromJSON(
                common::utils::CaseInsensitiveObjectWrapper(value), desiredStatus.mUpdateItems.Back());
        });

        common::utils::ForEach(json, "instances", [&desiredStatus](const auto& value) {
            auto err = desiredStatus.mInstances.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse instance");

            DesiredInstanceInfoFromJSON(
                common::utils::CaseInsensitiveObjectWrapper(value), desiredStatus.mInstances.Back());
        });

        common::utils::ForEach(json, "subjects", [&desiredStatus](const auto& value) {
            auto err = desiredStatus.mSubjects.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject");

            SubjectInfoFromJSON(common::utils::CaseInsensitiveObjectWrapper(value), desiredStatus.mSubjects.Back());
        });

        common::utils::ForEach(json, "certificates", [&desiredStatus](const auto& value) {
            auto err = desiredStatus.mCertificates.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate");

            CertificateInfoFromJSON(
                common::utils::CaseInsensitiveObjectWrapper(value), desiredStatus.mCertificates.Back());
        });

        common::utils::ForEach(json, "certificateChains", [&desiredStatus](const auto& value) {
            auto err = desiredStatus.mCertificateChains.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate chain");

            CertificateChainFromJSON(
                common::utils::CaseInsensitiveObjectWrapper(value), desiredStatus.mCertificateChains.Back());
        });
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
