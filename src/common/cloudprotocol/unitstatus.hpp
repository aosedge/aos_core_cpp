/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_UNITSTATUS_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_UNITSTATUS_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/unitstatus.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts UnitStatus object to JSON object.
 *
 * @param unitStatus UnitStatus object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const UnitStatus& unitStatus, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
