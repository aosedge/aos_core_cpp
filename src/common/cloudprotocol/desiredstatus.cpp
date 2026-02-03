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
#include "unitconfig.hpp"

namespace aos::common::cloudprotocol {

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
        auto err = LabelsFromJSON(json, instance.mLabels);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse labels");
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

            auto err = FromJSON(json.GetObject("unitConfig"), *desiredStatus.mUnitConfig);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse unitConfig");
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

} // namespace aos::common::cloudprotocol
