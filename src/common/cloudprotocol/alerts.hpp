/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_ALERTS_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_ALERTS_HPP_

#include <variant>

#include <Poco/JSON/Object.h>

#include <aos/common/cloudprotocol/alerts.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to alerts.
 *
 * @param json JSON object to parse.
 * @param[out] alerts object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::Alerts& alerts);

/**
 * Converts alerts to JSON object.
 *
 * @param alerts object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::Alerts& alerts, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
