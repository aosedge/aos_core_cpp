/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "servicediscovery.hpp"

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ToJSON(const ServiceDiscoveryRequest& request, Poco::JSON::Object& json)
{
    try {
        json.set("version", request.mVersion);
        json.set("systemId", request.mSystemID.CStr());
        json.set("supportedProtocols",
            common::utils::ToJsonArray(
                request.mSupportedProtocols, [](const auto& protocol) { return protocol.CStr(); }));
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const std::string& responseStr, ServiceDiscoveryResponse& response)
{
    try {
        auto [jsonVar, err] = common::utils::ParseJson(responseStr);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse service discovery response JSON");

        auto json = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

        response.mVersion = json.GetValue<size_t>("version", 0);

        err = response.mSystemID.Assign(json.GetValue<std::string>("systemId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse systemId");

        response.mNextRequestDelay = Time::cMilliseconds * (json.GetValue<size_t>("nextRequestDelay"));

        for (const auto& url : common::utils::GetArrayValue<std::string>(json, "connectionInfo")) {
            err = response.mConnectionInfo.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse connectionInfo");

            err = response.mConnectionInfo.Back().Assign(url.c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse connectionInfo");
        }

        err = response.mAuthToken.Assign(json.GetValue<std::string>("authToken").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse authToken");

        response.mErrorCode = static_cast<ServiceDiscoveryResponseErrorEnum>(json.GetValue<size_t>("errorCode"));
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
