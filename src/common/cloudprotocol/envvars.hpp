/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_ENVVARS_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_ENVVARS_HPP_

#include <Poco/JSON/Object.h>

#include <aos/common/cloudprotocol/envvars.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to override environment variables request.
 *
 * @param json json object representation.
 * @param[out] envVars override environment variables request to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::OverrideEnvVarsRequest& envVars);

/**
 * Converts override environment variables request to JSON object.
 *
 * @param envVars override environment variables request to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::OverrideEnvVarsRequest& envVars, Poco::JSON::Object& json);

/**
 * Converts JSON object to override environment variables statuses.
 *
 * @param json JSON object representation.
 * @param envVars[out] envVars override environment variables statuses to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::OverrideEnvVarsStatuses& envVars);

/**
 * Converts environment variables statuses to JSON object.
 *
 * @param envVars environment variables statuses to convert.
 * @param json[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::OverrideEnvVarsStatuses& envVars, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
