/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_CLOUDPROTOCOL_ALERTS_HPP_
#define AOS_CM_COMMUNICATION_CLOUDPROTOCOL_ALERTS_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/alerts.hpp>

#include <common/utils/json.hpp>

namespace aos::cm::communication::cloudprotocol {

/**
 * Converts alerts to JSON object.
 *
 * @param alerts object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const Alerts& alerts, Poco::JSON::Object& json);

} // namespace aos::cm::communication::cloudprotocol

#endif
