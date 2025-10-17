/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_CLOUDPROTOCOL_ENVVARS_HPP_
#define AOS_CM_COMMUNICATION_CLOUDPROTOCOL_ENVVARS_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/envvars.hpp>

#include <common/utils/json.hpp>

namespace aos::cm::communication::cloudprotocol {

/**
 * Converts JSON object to override environment variables request.
 *
 * @param json json object representation.
 * @param[out] envVars override environment variables request to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, OverrideEnvVarsRequest& envVars);

/**
 * Converts environment variables statuses to JSON object.
 *
 * @param envVars environment variables statuses to convert.
 * @param json[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const OverrideEnvVarsStatuses& envVars, Poco::JSON::Object& json);

} // namespace aos::cm::communication::cloudprotocol

#endif
