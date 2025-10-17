/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_CLOUDPROTOCOL_MONITORING_HPP_
#define AOS_CM_COMMUNICATION_CLOUDPROTOCOL_MONITORING_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/monitoring.hpp>

#include <common/utils/json.hpp>

namespace aos::cm::communication::cloudprotocol {

/**
 * Converts monitoring object to JSON object.
 *
 * @param monitoring monitoring object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const Monitoring& monitoring, Poco::JSON::Object& json);

} // namespace aos::cm::communication::cloudprotocol

#endif
