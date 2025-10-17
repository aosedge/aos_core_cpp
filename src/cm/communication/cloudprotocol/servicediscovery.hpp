/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_CLOUDPROTOCOL_SERVICEDISCOVERY_HPP_
#define AOS_CM_COMMUNICATION_CLOUDPROTOCOL_SERVICEDISCOVERY_HPP_

#include <Poco/JSON/Object.h>

#include <core/cm/communication/servicediscovery.hpp>

namespace aos::cm::communication::cloudprotocol {

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

} // namespace aos::cm::communication::cloudprotocol

#endif
