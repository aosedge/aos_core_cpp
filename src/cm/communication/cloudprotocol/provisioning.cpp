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
        auto err = ParseAosIdentityID(json.GetObject("node"), request.mNodeID);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

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
        json.set("node", CreateAosIdentity({response.mNodeID}));

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
        auto err = ParseAosIdentityID(json.GetObject("node"), request.mNodeID);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

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
        json.set("node", CreateAosIdentity({response.mNodeID}));

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
        auto err = ParseAosIdentityID(json.GetObject("node"), request.mNodeID);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node");

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
        json.set("node", CreateAosIdentity({response.mNodeID}));

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
