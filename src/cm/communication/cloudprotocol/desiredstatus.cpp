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
    auto err = ParseAosIdentityID(json.GetObject("item"), node.mNodeID);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

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
    auto err = ParseAosIdentityID(json.GetObject("nodeGroupSubject"), nodeConfig.mNodeType);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse nodeGroupSubject");

    err = ParseAosIdentityID(json.GetObject("node"), nodeConfig.mNodeID);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

    if (json.Has("alertRules")) {
        nodeConfig.mAlertRules.EmplaceValue(AlertRulesFromJSON(json.GetObject("alertRules")));
    }

    if (json.Has("resourceRatios")) {
        nodeConfig.mResourceRatios.EmplaceValue(ResourceRatiosFromJSON(json.GetObject("resourceRatios")));
    }

    if (json.Has("labels")) {
        LabelsFromJSON(json, nodeConfig.mLabels);
    }

    if (const auto priority = json.GetOptionalValue<uint64_t>("priority"); priority.has_value()) {
        nodeConfig.mPriority = *priority;
    }
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

void ArchInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, ArchInfo& archInfo)
{
    auto err = archInfo.mArchitecture.Assign(json.GetValue<std::string>("architecture").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse architecture");

    if (const auto variant = json.GetOptionalValue<std::string>("variant"); variant.has_value()) {
        archInfo.mVariant.EmplaceValue();

        err = archInfo.mVariant->Assign(variant->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse arch variant");
    }
}

void OSInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, OSInfo& osInfo)
{
    auto err = osInfo.mOS.Assign(json.GetValue<std::string>("os").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse os");

    if (const auto version = json.GetOptionalValue<std::string>("version"); version.has_value()) {
        osInfo.mVersion.EmplaceValue();

        err = osInfo.mVersion->Assign(version->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse os version");
    }

    common::utils::ForEach(json, "features", [&osInfo](const Poco::Dynamic::Var& value) {
        auto err = osInfo.mFeatures.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse os features");

        err = osInfo.mFeatures.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse os feature");
    });
}

void ImageInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, ImageInfo& imageInfo)
{
    auto err = imageInfo.mImageID.Assign(json.GetValue<std::string>("id").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse image id");

    ArchInfoFromJSON(json.GetObject("archInfo"), imageInfo.mArchInfo);
    OSInfoFromJSON(json.GetObject("osInfo"), imageInfo.mOSInfo);
}

void DecryptInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, crypto::DecryptInfo& decryptInfo)
{
    auto err = decryptInfo.mBlockAlg.Assign(json.GetValue<std::string>("blockAlg").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse blockAlg");

    const auto iv = Base64Decode(json.GetValue<std::string>("blockIv"));
    err           = decryptInfo.mBlockIV.Assign(String(iv.c_str()).AsByteArray());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse blockIv");

    const auto key = Base64Decode(json.GetValue<std::string>("blockKey"));
    err            = decryptInfo.mBlockKey.Assign(String(key.c_str()).AsByteArray());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse blockKey");
}

void SignInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, crypto::SignInfo& signInfo)
{
    auto err = signInfo.mChainName.Assign(json.GetValue<std::string>("chainName").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse signInfo chainName");

    err = signInfo.mAlg.Assign(json.GetValue<std::string>("alg").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse signInfo alg");

    const auto value = Base64Decode(json.GetValue<std::string>("value"));
    err              = signInfo.mValue.Assign(String(value.c_str()).AsByteArray());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse signInfo value");

    const auto trustedTimestamp = json.GetOptionalValue<std::string>("trustedTimestamp");

    if (!trustedTimestamp.has_value()) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "trustedTimestamp is missing in signInfo JSON");
    }

    Tie(signInfo.mTrustedTimestamp, err) = Time::UTC(trustedTimestamp->c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "trusted timestamp parsing error");

    common::utils::ForEach(json, "ocspValues", [&signInfo](const Poco::Dynamic::Var& value) {
        auto err = signInfo.mOCSPValues.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse ocsp value");

        err = signInfo.mOCSPValues.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse ocsp value");
    });
}

void UpdateImageInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, UpdateImageInfo& updateImageInfo)
{
    ImageInfoFromJSON(json.GetObject("image"), updateImageInfo.mImage);

    common::utils::ForEach(json, "urls", [&updateImageInfo](const Poco::Dynamic::Var& value) {
        auto err = updateImageInfo.mURLs.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse image URL");

        err = updateImageInfo.mURLs.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse image URL");
    });

    const auto sha256 = json.GetValue<std::string>("sha256");

    auto err = String(sha256.c_str()).HexToByteArray(updateImageInfo.mSHA256);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse image sha256");

    updateImageInfo.mSize = json.GetValue<size_t>("size");

    DecryptInfoFromJSON(json.GetObject("decryptInfo"), updateImageInfo.mDecryptInfo);
    SignInfoFromJSON(json.GetObject("signInfo"), updateImageInfo.mSignInfo);
}

void UpdateItemInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, UpdateItemInfo& updateItemInfo)
{
    auto err = ParseAosIdentityID(json.GetObject("item"), updateItemInfo.mItemID);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

    err = updateItemInfo.mVersion.Assign(json.GetValue<std::string>("version").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse version");

    err = ParseAosIdentityID(json.GetObject("owner"), updateItemInfo.mOwnerID);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse owner");

    common::utils::ForEach(json, "images", [&updateItemInfo](const auto& value) {
        auto err = updateItemInfo.mImages.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse image");

        UpdateImageInfoFromJSON(common::utils::CaseInsensitiveObjectWrapper(value), updateItemInfo.mImages.Back());
    });
}

void DesiredInstanceInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, DesiredInstanceInfo& instance)
{
    auto err = ParseAosIdentityID(json.GetObject("item"), instance.mItemID);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

    err = ParseAosIdentityID(json.GetObject("subject"), instance.mSubjectID);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject");

    instance.mPriority     = json.GetValue<size_t>("priority");
    instance.mNumInstances = json.GetValue<size_t>("numInstances");

    if (json.Has("labels")) {
        LabelsFromJSON(json, instance.mLabels);
    }
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
