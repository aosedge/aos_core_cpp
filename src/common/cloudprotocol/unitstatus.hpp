/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_UNITSTATUS_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_UNITSTATUS_HPP_

#include <variant>

#include <Poco/JSON/Object.h>

#include <core/common/cloudprotocol/unitstatus.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to UnitStatus object.
 *
 * @param json json object representation.
 * @param[out] unitStatus UnitStatus object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::UnitStatus& unitStatus);

/**
 * Converts UnitStatus object to JSON object.
 *
 * @param unitStatus UnitStatus object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::UnitStatus& unitStatus, Poco::JSON::Object& json);

/**
 * Converts JSON object to DeltaUnitStatus object.
 *
 * @param json json object representation.
 * @param[out] deltaUnitStatus delta unit status object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DeltaUnitStatus& deltaUnitStatus);

/**
 * Converts DeltaUnitStatus object to JSON object.
 *
 * @param deltaUnitStatus delta unit status object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::DeltaUnitStatus& deltaUnitStatus, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
