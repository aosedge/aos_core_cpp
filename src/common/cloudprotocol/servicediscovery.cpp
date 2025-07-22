/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "servicediscovery.hpp"

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::ServiceDiscoveryRequest& request)
{
    try {
        request.mVersion = json.GetValue<size_t>("version", 0);

        auto err = request.mSystemID.Assign(json.GetValue<std::string>("systemId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse systemId field");

        for (const auto& protocol : utils::GetArrayValue<std::string>(json, "supportedProtocols")) {
            err = request.mSupportedProtocols.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse supportedProtocols field");

            err = request.mSupportedProtocols.Back().Assign(protocol.c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse supportedProtocols field");
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::ServiceDiscoveryRequest& request, Poco::JSON::Object& json)
{
    try {
        json.set("version", request.mVersion);
        json.set("systemId", request.mSystemID.CStr());
        json.set("supportedProtocols",
            utils::ToJsonArray(request.mSupportedProtocols, [&](const auto& proto) { return proto.CStr(); }));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::ServiceDiscoveryResponse& response)
{
    try {
        response.mVersion = json.GetValue<size_t>("version", 0);

        auto err = response.mSystemID.Assign(json.GetValue<std::string>("systemId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse systemId field");

        response.mNextRequestDelay = Time::cMilliseconds * (json.GetValue<int32_t>("nextRequestDelay"));

        for (const auto& url : utils::GetArrayValue<std::string>(json, "connectionInfo")) {
            err = response.mConnectionInfo.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse connectionInfo field");

            err = response.mConnectionInfo.Back().Assign(url.c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse connectionInfo field");
        }

        err = response.mAuthToken.Assign(json.GetValue<std::string>("authToken").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse authToken field");

        response.mErrorCode
            = static_cast<aos::cloudprotocol::ServiceDiscoveryResponseErrorEnum>(json.GetValue<size_t>("errorCode"));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::ServiceDiscoveryResponse& response, Poco::JSON::Object& json)
{
    try {
        json.set("version", response.mVersion);
        json.set("systemId", response.mSystemID.CStr());
        json.set("nextRequestDelay", response.mNextRequestDelay.Milliseconds());
        json.set("connectionInfo",
            utils::ToJsonArray(response.mConnectionInfo, [&](const auto& url) { return url.CStr(); }));
        json.set("authToken", response.mAuthToken.CStr());
        json.set("errorCode", static_cast<size_t>(response.mErrorCode.GetValue()));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
