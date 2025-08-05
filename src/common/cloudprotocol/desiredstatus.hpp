/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_DESIREDSTATUS_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_DESIREDSTATUS_HPP_

#include <variant>

#include <Poco/JSON/Object.h>

#include <core/common/cloudprotocol/desiredstatus.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to node config object.
 *
 * @param json json object representation.
 * @param[out] nodeConfig node config object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::NodeConfig& nodeConfig);

/**
 * Converts node config object to JSON object.
 *
 * @param nodeConfig node config object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::NodeConfig& nodeConfig, Poco::JSON::Object& json);

/**
 * Converts JSON object to desired status object.
 *
 * @param json json object representation.
 * @param[out] desiredStatus desired status object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DesiredStatus& desiredStatus);

/**
 * Converts DeltaUnitStatus object to JSON object.
 *
 * @param deltaUnitStatus delta unit status object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::DesiredStatus& desiredStatus, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
