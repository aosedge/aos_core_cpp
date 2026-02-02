/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_ALERTS_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_ALERTS_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/alerts.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts alerts to JSON object.
 *
 * @param alerts object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const Alerts& alerts, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
