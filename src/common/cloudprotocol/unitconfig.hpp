/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_UNITCONFIG_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_UNITCONFIG_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/unitconfig.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts NodeConfig object to JSON object.
 *
 * @param nodeConfig node config object.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const NodeConfig& nodeConfig, Poco::JSON::Object& json);

/**
 * Creates NodeConfig object from JSON string.
 *
 * @param json JSON object.
 * @param[out] nodeConfig node config object.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, NodeConfig& nodeConfig);

/**
 * Converts UnitConfig object to JSON object.
 *
 * @param unitConfig unit config object.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const UnitConfig& unitConfig, Poco::JSON::Object& json);

/**
 * Creates UnitConfig object from JSON string.
 *
 * @param json JSON object.
 * @param[out] unitConfig unit config object.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, UnitConfig& unitConfig);

} // namespace aos::common::cloudprotocol

#endif
