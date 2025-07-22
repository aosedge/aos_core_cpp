/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_MONITORING_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_MONITORING_HPP_

#include <variant>

#include <Poco/JSON/Object.h>

#include <aos/common/cloudprotocol/monitoring.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to monitoring object.
 *
 * @param json json object representation.
 * @param[out] monitoring monitoring object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::Monitoring& monitoring);

/**
 * Converts monitoring object to JSON object.
 *
 * @param monitoring monitoring object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::Monitoring& monitoring, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
