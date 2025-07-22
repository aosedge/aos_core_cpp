/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <aos/common/cloudprotocol/cloudmessage.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "certificates.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr InstallCertDataToJSON(const aos::cloudprotocol::InstallCertData& installCertData)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("type", installCertData.mType.ToString().CStr());
    json->set("nodeId", installCertData.mNodeID.CStr());
    json->set("serial", installCertData.mSerial.CStr());
    json->set("status", installCertData.mStatus.ToString().CStr());
    json->set("description", installCertData.mDescription.CStr());

    return json;
}

void InstallCertDataFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::InstallCertData& installCertData)
{
    auto err = installCertData.mType.FromString(json.GetValue<std::string>("type").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed parsing type field");

    err = installCertData.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

    err = installCertData.mSerial.Assign(json.GetValue<std::string>("serial").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed parsing serial field");

    err = installCertData.mStatus.FromString(json.GetValue<std::string>("status").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed parsing status field");

    err = installCertData.mDescription.Assign(json.GetValue<std::string>("description").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed parsing description field");
}

Poco::JSON::Object::Ptr RenewCertDataToJSON(const aos::cloudprotocol::RenewCertData& renewCertData)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("type", renewCertData.mType.ToString().CStr());
    json->set("nodeId", renewCertData.mNodeID.CStr());
    json->set("serial", renewCertData.mSerial.CStr());

    if (renewCertData.mValidTill.HasValue()) {
        auto time = renewCertData.mValidTill->ToUTCString();
        if (!time.mError.IsNone()) {
            AOS_ERROR_CHECK_AND_THROW(time.mError, "failed to convert validTill time to UTC string");
        }

        json->set("validTill", time.mValue.CStr());
    }

    return json;
}

void RenewCertDataFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::RenewCertData& renewCertData)
{
    auto err = renewCertData.mType.FromString(json.GetValue<std::string>("type").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed parsing type field");

    err = renewCertData.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

    err = renewCertData.mSerial.Assign(json.GetValue<std::string>("serial").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed parsing serial field");

    if (json.Has("validTill")) {
        auto time = Time::UTC(json.GetValue<std::string>("validTill").c_str());
        AOS_ERROR_CHECK_AND_THROW(time.mError, "failed parsing validTill field");

        renewCertData.mValidTill.EmplaceValue(time.mValue);
    }
}

Poco::JSON::Object::Ptr UnitSecretsToJSON(const aos::cloudprotocol::UnitSecrets& unitSecrets)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("version", unitSecrets.mVersion.CStr());

    auto nodes = Poco::makeShared<Poco::JSON::Object>();
    for (const auto& node : unitSecrets.mNodes) {
        nodes->set(node.mFirst.CStr(), node.mSecond.CStr());
    }

    if (nodes->size() > 0) {
        json->set("nodes", nodes);
    }

    return json;
}

void UnitSecretsFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::UnitSecrets& unitSecrets)
{
    unitSecrets.mVersion.Assign(json.GetValue<std::string>("version").c_str());

    if (json.Has("nodes")) {
        auto nodesObject = json.Get("nodes").extract<Poco::JSON::Object::Ptr>();

        for (const auto& node : *nodesObject) {
            auto err = unitSecrets.mNodes.Emplace(node.first.c_str(), node.second.toString().c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse nodes object");
        }
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::IssueCertData& issueCertData)
{
    try {
        auto err = issueCertData.mType.FromString(json.GetValue<std::string>("type").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing type field");

        err = issueCertData.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

        err = issueCertData.mCsr.Assign(json.GetValue<std::string>("csr").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing csr field");
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::IssueCertData& issueCertData, Poco::JSON::Object& json)
{
    try {
        json.set("type", issueCertData.mType.ToString().CStr());
        json.set("nodeId", issueCertData.mNodeID.CStr());
        json.set("csr", issueCertData.mCsr.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::IssuedCertData& issuedCertData)
{
    try {
        auto err = issuedCertData.mType.FromString(json.GetValue<std::string>("type").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing type field");

        err = issuedCertData.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

        err = issuedCertData.mCertificateChain.Assign(json.GetValue<std::string>("certificateChain").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing certificateChain field");
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::IssuedCertData& issuedCertData, Poco::JSON::Object& json)
{
    try {
        json.set("type", issuedCertData.mType.ToString().CStr());
        json.set("nodeId", issuedCertData.mNodeID.CStr());
        json.set("certificateChain", issuedCertData.mCertificateChain.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::RenewCertsNotification& renewCertsNotification)
{
    try {
        if (!json.Has("unitSecrets")) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "unitSecrets field is required"));
        }

        UnitSecretsFromJSON(json.GetObject("unitSecrets"), renewCertsNotification.mUnitSecrets);

        utils::ForEach(json, "certificates", [&renewCertsNotification](const auto& certJson) {
            auto err = renewCertsNotification.mCertificates.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to emplace back certificate data");

            RenewCertDataFromJSON(
                utils::CaseInsensitiveObjectWrapper(certJson), renewCertsNotification.mCertificates.Back());
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::RenewCertsNotification& renewCertsNotification, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageTye
        = aos::cloudprotocol::MessageTypeEnum::eRenewCertificatesNotification;

    try {
        json.set("messageType", cMessageTye.ToString().CStr());
        json.set("certificates", utils::ToJsonArray(renewCertsNotification.mCertificates, RenewCertDataToJSON));
        json.set("unitSecrets", UnitSecretsToJSON(renewCertsNotification.mUnitSecrets));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::IssuedUnitCerts& issuedUnitCerts)
{
    try {
        utils::ForEach(json, "certificates", [&issuedUnitCerts](const auto& certJson) {
            auto err = issuedUnitCerts.mCertificates.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to emplace back issued certificate data");

            err = FromJSON(utils::CaseInsensitiveObjectWrapper(certJson), issuedUnitCerts.mCertificates.Back());
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse issued certificate data from JSON");
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::IssuedUnitCerts& issuedUnitCerts, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageTye
        = aos::cloudprotocol::MessageTypeEnum::eIssuedUnitCertificates;

    try {
        json.set("messageType", cMessageTye.ToString().CStr());
        json.set("certificates", utils::ToJsonArray(issuedUnitCerts.mCertificates, [](const auto& cert) {
            auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(cert, *json);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert issued certificate data to JSON");

            return json;
        }));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::IssueUnitCerts& issueUnitCerts)
{
    try {
        utils::ForEach(json, "requests", [&issueUnitCerts](const auto& certJson) {
            auto err = issueUnitCerts.mRequests.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to emplace back issue certificate data");

            err = FromJSON(utils::CaseInsensitiveObjectWrapper(certJson), issueUnitCerts.mRequests.Back());
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse issue certificate data from JSON");
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::IssueUnitCerts& issueUnitCerts, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType
        = aos::cloudprotocol::MessageTypeEnum::eIssueUnitCertificates;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("requests", utils::ToJsonArray(issueUnitCerts.mRequests, [](const auto& request) {
            auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(request, *json);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert issue certificate data to JSON");

            return json;
        }));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::InstallUnitCertsConfirmation& confirmation)
{
    try {
        utils::ForEach(json, "certificates", [&confirmation](const auto& certJson) {
            auto err = confirmation.mCertificates.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to emplace back certificate data");

            InstallCertDataFromJSON(utils::CaseInsensitiveObjectWrapper(certJson), confirmation.mCertificates.Back());
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::InstallUnitCertsConfirmation& confirmation, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageTye
        = aos::cloudprotocol::MessageTypeEnum::eInstallUnitCertificatesConfirmation;

    try {
        json.set("messageType", cMessageTye.ToString().CStr());
        json.set("certificates", utils::ToJsonArray(confirmation.mCertificates, InstallCertDataToJSON));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
