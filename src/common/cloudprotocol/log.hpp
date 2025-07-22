/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_LOG_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_LOG_HPP_

#include <Poco/JSON/Object.h>

#include <aos/common/cloudprotocol/log.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to push log object.
 *
 * @param json json object representation.
 * @param[out] pushLog push log object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::PushLog& pushLog);

/**
 * Converts push log object to JSON object.
 *
 * @param pushLog push log object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::PushLog& pushLog, Poco::JSON::Object& json);

/**
 * Converts JSON object to request log object.
 *
 * @param json json object representation.
 * @param[out] requestLog request log object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::RequestLog& requestLog);

/**
 * Converts request log object to JSON object.
 *
 * @param requestLog request log object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::RequestLog& requestLog, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
