/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_SERVICEDISCOVERY_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_SERVICEDISCOVERY_HPP_

#include <string>
#include <vector>

#include <Poco/JSON/Object.h>

#include <core/common/tools/time.hpp>

namespace aos::common::cloudprotocol {

/**
 * Service discovery request.
 */
struct ServiceDiscoveryRequest {
    size_t                   mVersion {};
    std::string              mSystemID;
    std::vector<std::string> mSupportedProtocols;

    /**
     * Compares service discovery request.
     *
     * @param lhs service discovery request.
     * @param rhs service discovery request to compare with.
     * @return bool.
     */
    friend bool operator==(const ServiceDiscoveryRequest& lhs, const ServiceDiscoveryRequest& rhs)
    {
        return lhs.mVersion == rhs.mVersion && lhs.mSystemID == rhs.mSystemID
            && lhs.mSupportedProtocols == rhs.mSupportedProtocols;
    }

    /**
     * Compares service discovery request.
     *
     * @param lhs service discovery request.
     * @param rhs service discovery request to compare with.
     * @return bool.
     */
    friend bool operator!=(const ServiceDiscoveryRequest& lhs, const ServiceDiscoveryRequest& rhs)
    {
        return !(lhs == rhs);
    }
};

/**
 * Service discovery response error code.
 */
class ServiceDiscoveryResponseErrorType {
public:
    enum class Enum {
        eNoError,
        eRedirect,
        eRepeatLater,
        eError,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sStrings[] = {
            "NoError",
            "Redirect",
            "RepeatLater",
            "Error",
        };
        return Array<const char* const>(sStrings, ArraySize(sStrings));
    };
};

using ServiceDiscoveryResponseErrorEnum = ServiceDiscoveryResponseErrorType::Enum;
using ServiceDiscoveryResponseError     = EnumStringer<ServiceDiscoveryResponseErrorType>;

/**
 * Service discovery response.
 */
struct ServiceDiscoveryResponse {
    size_t                        mVersion {};
    std::string                   mSystemID;
    Duration                      mNextRequestDelay;
    std::vector<std::string>      mConnectionInfo;
    std::string                   mAuthToken;
    ServiceDiscoveryResponseError mErrorCode;

    /**
     * Compares service discovery response.
     *
     * @param lhs service discovery response.
     * @param rhs service discovery response to compare with.
     * @return bool.
     */
    friend bool operator==(const ServiceDiscoveryResponse& lhs, const ServiceDiscoveryResponse& rhs)
    {
        return lhs.mVersion == rhs.mVersion && lhs.mSystemID == rhs.mSystemID
            && lhs.mNextRequestDelay == rhs.mNextRequestDelay && lhs.mConnectionInfo == rhs.mConnectionInfo
            && lhs.mAuthToken == rhs.mAuthToken && lhs.mErrorCode == rhs.mErrorCode;
    }

    /**
     * Compares service discovery response.
     *
     * @param lhs service discovery response.
     * @param rhs service discovery response to compare with.
     * @return bool.
     */
    friend bool operator!=(const ServiceDiscoveryResponse& lhs, const ServiceDiscoveryResponse& rhs)
    {
        return !(lhs == rhs);
    }
};

/**
 * Converts service discovery request to JSON object.
 *
 * @param request object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const ServiceDiscoveryRequest& request, Poco::JSON::Object& json);

/**
 * Converts JSON string to service discovery response type.
 *
 * @param responseStr JSON string to parse.
 * @param[out] response object to fill.
 * @return Error.
 */
Error FromJSON(const std::string& responseStr, ServiceDiscoveryResponse& response);

} // namespace aos::common::cloudprotocol

#endif
