/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_CLOUDPROTOCOL_LOG_HPP_
#define AOS_CM_COMMUNICATION_CLOUDPROTOCOL_LOG_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/log.hpp>

#include <common/utils/json.hpp>

namespace aos::cm::communication::cloudprotocol {

/**
 * Converts push log object to JSON object.
 *
 * @param pushLog push log object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const PushLog& pushLog, Poco::JSON::Object& json);

/**
 * Converts JSON object to request log object.
 *
 * @param json json object representation.
 * @param[out] requestLog request log object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, RequestLog& requestLog);

} // namespace aos::cm::communication::cloudprotocol

#endif
