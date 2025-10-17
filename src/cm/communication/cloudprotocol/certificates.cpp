/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>

#include "certificates.hpp"
#include "common.hpp"

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr CertIdentToJSON(const CertIdent& certIdent)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("type", certIdent.mType.ToString().CStr());
    json->set("node", CreateAosIdentity({certIdent.mNodeID}));

    return json;
}

void CertIdentFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, CertIdent& certIdent)
{
    if (json.Has("type")) {
        auto err = certIdent.mType.FromString(json.GetValue<std::string>("type").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse type");
    }

    if (!json.Has("node")) {
        AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "missing node tag");
    }

    auto err = ParseAosIdentityID(json.GetObject("node"), certIdent.mNodeID);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse id");
}

void NodeSecretFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, NodeSecret& nodeSecret)
{
    if (!json.Has("node")) {
        AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "missing node tag");
    }

    auto err = ParseAosIdentityID(json.GetObject("node"), nodeSecret.mNodeID);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse id");

    err = nodeSecret.mSecret.Assign(json.GetValue<std::string>("secret").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse secret");
}

void UnitSecretsFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, UnitSecrets& unitSecrets)
{
    auto err = unitSecrets.mVersion.Assign(json.GetValue<std::string>("version").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse version");

    common::utils::ForEach(json, "nodes", [&unitSecrets](const auto& nodeSecretJson) {
        auto err = unitSecrets.mNodes.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node secret");

        NodeSecretFromJSON(common::utils::CaseInsensitiveObjectWrapper(nodeSecretJson), unitSecrets.mNodes.Back());
    });
}

void IssuedCertDataFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, IssuedCertData& issuedCertData)
{
    CertIdentFromJSON(json, issuedCertData);

    auto err = issuedCertData.mCertificateChain.Assign(json.GetValue<std::string>("certificateChain").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificateChain");
}

void RenewCertDataFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, RenewCertData& renewCertData)
{
    CertIdentFromJSON(json, renewCertData);

    auto err = renewCertData.mSerial.Assign(json.GetValue<std::string>("serial").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse serial");

    if (json.Has("validTill")) {
        auto result = Time::UTC(json.GetValue<std::string>("validTill").c_str());
        AOS_ERROR_CHECK_AND_THROW(result.mError, "can't parse validTill");

        renewCertData.mValidTill.EmplaceValue(result.mValue);
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, RenewCertsNotification& renewCertsNotification)
{
    try {
        if (!json.Has("unitSecrets")) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "unitSecrets field is required"));
        }

        UnitSecretsFromJSON(json.GetObject("unitSecrets"), renewCertsNotification.mUnitSecrets);

        common::utils::ForEach(json, "certificates", [&renewCertsNotification](const auto& certJson) {
            auto err = renewCertsNotification.mCertificates.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate");

            RenewCertDataFromJSON(
                common::utils::CaseInsensitiveObjectWrapper(certJson), renewCertsNotification.mCertificates.Back());
        });
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, IssuedUnitCerts& issuedUnitCerts)
{
    try {
        common::utils::ForEach(json, "certificates", [&issuedUnitCerts](const auto& certJson) {
            auto err = issuedUnitCerts.mCertificates.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate");

            IssuedCertDataFromJSON(
                common::utils::CaseInsensitiveObjectWrapper(certJson), issuedUnitCerts.mCertificates.Back());
        });
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const IssueUnitCerts& issueUnitCerts, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eIssueUnitCertificates;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("requests", common::utils::ToJsonArray(issueUnitCerts.mRequests, [](const auto& request) {
            auto json = CertIdentToJSON(request);

            json->set("csr", request.mCSR.CStr());

            return json;
        }));
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const InstallUnitCertsConfirmation& confirmation, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eInstallUnitCertificatesConfirmation;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("certificates", common::utils::ToJsonArray(confirmation.mCertificates, [](const auto& certStatus) {
            auto certJson = CertIdentToJSON(certStatus);

            certJson->set("serial", certStatus.mSerial.CStr());

            if (!certStatus.mError.IsNone()) {
                auto errorJSON = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

                auto err = ToJSON(certStatus.mError, *errorJSON);
                AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

                certJson->set("errorInfo", errorJSON);
            }

            return certJson;
        }));
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
