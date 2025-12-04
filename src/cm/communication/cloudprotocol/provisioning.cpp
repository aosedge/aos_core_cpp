/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "certificates.hpp"
#include "common.hpp"
#include "provisioning.hpp"

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, StartProvisioningRequest& request)
{
    try {
        AosIdentity identity;

        auto err = ParseAosIdentity(json.GetObject("node"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

        if (!identity.mCodename.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "node codename is missing");
        }

        err = request.mNodeID.Assign(identity.mCodename->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node ID");

        err = FromJSON(json, static_cast<Protocol&>(request));
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse protocol");

        err = request.mPassword.Assign(json.GetValue<std::string>("password").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse password");
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const StartProvisioningResponse& response, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eStartProvisioningResponse;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(response), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        AosIdentity identity;
        identity.mCodename = response.mNodeID.CStr();

        json.set("node", CreateAosIdentity(identity));

        if (!response.mError.IsNone()) {
            auto errorJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(response.mError, *errorJson);
            AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

            json.set("errorInfo", errorJson);
        }

        json.set("csrs", common::utils::ToJsonArray(response.mCSRs, [](const auto& csr) {
            auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            json->set("type", csr.mType.ToString().CStr());
            json->set("csr", csr.mCSR.CStr());

            return json;
        }));
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, FinishProvisioningRequest& request)
{
    try {
        AosIdentity identity;

        auto err = ParseAosIdentity(json.GetObject("node"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

        if (!identity.mCodename.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "node codename is missing");
        }

        err = request.mNodeID.Assign(identity.mCodename->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node ID");

        err = FromJSON(json, static_cast<Protocol&>(request));
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse protocol");

        if (!json.Has("certificates")) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "certificates tag is required"));
        }

        common::utils::ForEach(json, "certificates", [&request](const auto& certJson) {
            auto err = request.mCertificates.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parese certificate");

            auto wrapper = common::utils::CaseInsensitiveObjectWrapper(certJson);

            err = request.mCertificates.Back().mCertType.FromString(wrapper.GetValue<std::string>("type").c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate type");

            err = request.mCertificates.Back().mCertChain.Assign(wrapper.GetValue<std::string>("chain").c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse certificate chain");
        });

        err = request.mPassword.Assign(json.GetValue<std::string>("password").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse password");
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const FinishProvisioningResponse& response, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eFinishProvisioningResponse;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(response), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        AosIdentity identity;
        identity.mCodename = response.mNodeID.CStr();

        json.set("node", CreateAosIdentity(identity));

        if (!response.mError.IsNone()) {
            auto errorJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(response.mError, *errorJson);
            AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

            json.set("errorInfo", errorJson);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, DeprovisioningRequest& request)
{
    try {
        AosIdentity identity;

        auto err = ParseAosIdentity(json.GetObject("node"), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

        if (!identity.mCodename.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "node codename is missing");
        }

        err = request.mNodeID.Assign(identity.mCodename->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node ID");

        err = FromJSON(json, static_cast<Protocol&>(request));
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse protocol");

        err = request.mPassword.Assign(json.GetValue<std::string>("password").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse password");
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const DeprovisioningResponse& response, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eDeprovisioningResponse;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(response), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        AosIdentity identity;
        identity.mCodename = response.mNodeID.CStr();

        json.set("node", CreateAosIdentity(identity));

        if (!response.mError.IsNone()) {
            auto errorJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(response.mError, *errorJson);
            AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

            json.set("errorInfo", errorJson);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
