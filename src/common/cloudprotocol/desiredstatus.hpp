/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_DESIREDSTATUS_HPP_
#define AOS_COMMON_DESIREDSTATUS_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/desiredstatus.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to desired status object.
 *
 * @param json json object representation.
 * @param[out] desiredStatus desired status object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, DesiredStatus& desiredStatus);

} // namespace aos::common::cloudprotocol

#endif
