/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <core/common/cloudprotocol/cloudmessage.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "certificates.hpp"
#include "common.hpp"
#include "provisioning.hpp"

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::StartProvisioningRequest& request)
{
    try {
        auto err = request.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

        err = request.mPassword.Assign(json.GetValue<std::string>("password").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing password field");
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::StartProvisioningRequest& request, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType(
        aos::cloudprotocol::MessageTypeEnum::eStartProvisioningRequest);

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("nodeId", request.mNodeID.CStr());
        json.set("password", request.mPassword.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::StartProvisioningResponse& response)
{
    try {
        auto err = response.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

        if (json.Has("errorInfo")) {
            err = FromJSON(json.GetObject("errorInfo"), response.mError);
            AOS_ERROR_CHECK_AND_THROW(err, "failed parsing errorInfo field");
        }

        if (!json.Has("csrs")) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "csrs field is required"));
        }

        utils::ForEach(json, "csrs", [&response](const auto& csrJson) {
            auto err = response.mCSRs.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to emplace back issue certificate data");

            err = FromJSON(utils::CaseInsensitiveObjectWrapper(csrJson), response.mCSRs.Back());
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse issue certificate data from JSON");
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::StartProvisioningResponse& response, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType(
        aos::cloudprotocol::MessageTypeEnum::eStartProvisioningResponse);

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("nodeId", response.mNodeID.CStr());

        if (!response.mError.IsNone()) {
            auto errorJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(response.mError, *errorJson);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

            json.set("errorInfo", errorJson);
        }

        json.set("csrs", utils::ToJsonArray(response.mCSRs, [](const auto& csr) {
            auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(csr, *json);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert issue certificate data to JSON");

            return json;
        }));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::FinishProvisioningRequest& request)
{
    try {
        auto err = request.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

        if (!json.Has("certificates")) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "certificates field is required"));
        }

        utils::ForEach(json, "certificates", [&request](const auto& certJson) {
            auto err = request.mCertificates.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to emplace back issued certificate data");

            err = FromJSON(utils::CaseInsensitiveObjectWrapper(certJson), request.mCertificates.Back());
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse issued certificate data from JSON");
        });

        err = request.mPassword.Assign(json.GetValue<std::string>("password").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing password field");
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::FinishProvisioningRequest& request, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType
        = aos::cloudprotocol::MessageTypeEnum::eFinishProvisioningRequest;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("nodeId", request.mNodeID.CStr());
        json.set("password", request.mPassword.CStr());

        json.set("certificates", utils::ToJsonArray(request.mCertificates, [](const auto& cert) {
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

Error FromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::FinishProvisioningResponse& response)
{
    try {
        auto err = response.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

        if (json.Has("errorInfo")) {
            err = FromJSON(json.GetObject("errorInfo"), response.mError);
            AOS_ERROR_CHECK_AND_THROW(err, "failed parsing errorInfo field");
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::FinishProvisioningResponse& response, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType
        = aos::cloudprotocol::MessageTypeEnum::eFinishProvisioningResponse;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("nodeId", response.mNodeID.CStr());

        if (!response.mError.IsNone()) {
            auto errorJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(response.mError, *errorJson);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

            json.set("errorInfo", errorJson);
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DeprovisioningRequest& request)
{
    try {
        auto err = request.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

        err = request.mPassword.Assign(json.GetValue<std::string>("password").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing password field");
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::DeprovisioningRequest& request, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType
        = aos::cloudprotocol::MessageTypeEnum::eDeprovisioningRequest;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("nodeId", request.mNodeID.CStr());
        json.set("password", request.mPassword.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DeprovisioningResponse& response)
{
    try {
        auto err = response.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed parsing nodeId field");

        if (json.Has("errorInfo")) {
            err = FromJSON(json.GetObject("errorInfo"), response.mError);
            AOS_ERROR_CHECK_AND_THROW(err, "failed parsing errorInfo field");
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::DeprovisioningResponse& response, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType
        = aos::cloudprotocol::MessageTypeEnum::eDeprovisioningResponse;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("nodeId", response.mNodeID.CStr());

        if (!response.mError.IsNone()) {
            auto errorJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            auto err = ToJSON(response.mError, *errorJson);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

            json.set("errorInfo", errorJson);
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
