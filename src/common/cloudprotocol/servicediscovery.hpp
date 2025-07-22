/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_SERVICEDISCOVERY_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_SERVICEDISCOVERY_HPP_

#include <variant>

#include <Poco/JSON/Object.h>

#include <aos/common/cloudprotocol/servicediscovery.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to service discovery request.
 *
 * @param json JSON object to parse.
 * @param[out] request object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::ServiceDiscoveryRequest& request);

/**
 * Converts service discovery request to JSON object.
 *
 * @param request object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::ServiceDiscoveryRequest& request, Poco::JSON::Object& json);

/**
 * Converts JSON object to service discovery response type.
 *
 * @param json JSON object to parse.
 * @param[out] response object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::ServiceDiscoveryResponse& response);

/**
 * Converts service discovery response to JSON object.
 *
 * @param response object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::ServiceDiscoveryResponse& response, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
