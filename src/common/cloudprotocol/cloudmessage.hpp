/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_CLOUDMESSAGE_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_CLOUDMESSAGE_HPP_

#include <variant>

#include <Poco/JSON/Object.h>

#include <aos/common/cloudprotocol/cloudmessage.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to cloud message header.
 *
 * @param json JSON object to parse.
 * @param[out] header Message header to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::MessageHeader& header);

/**
 * Converts cloud message header to JSON object.
 *
 * @param header Message header to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::MessageHeader& header, Poco::JSON::Object& json);

/**
 * Converts JSON string to CloudMessage object.
 *
 * @param json JSON string to parse.
 * @param[out] message CloudMessage object to fill.
 * @return Error.
 */
Error FromJSON(const std::string& json, aos::cloudprotocol::CloudMessage& message);

/**
 * Converts CloudMessage object to JSON string.
 *
 * @param message CloudMessage object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::CloudMessage& message, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
