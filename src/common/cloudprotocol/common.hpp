/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_COMMON_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_COMMON_HPP_

#include <variant>

#include <Poco/JSON/Object.h>

#include <core/common/cloudprotocol/common.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to Error object.
 *
 * @param json json object representation.
 * @param[out] error Error object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, Error& error);

/**
 * Converts Error object to JSON object.
 *
 * @param error Error object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const Error& error, Poco::JSON::Object& json);

/**
 * Converts JSON object to InstanceIdent object.
 *
 * @param json json object representation.
 * @param[out] instanceIdent instance ident object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, InstanceIdent& instanceIdent);

/**
 * Converts InstanceIdent object to JSON object.
 *
 * @param instanceIdent instance ident object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const InstanceIdent& instanceIdent, Poco::JSON::Object& json);

/**
 * Converts JSON object to InstanceFilter object.
 *
 * @param json json object representation.
 * @param[out] instanceFilter instance filter object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::InstanceFilter& instanceFilter);

/**
 * Converts InstanceFilter object to JSON object.
 *
 * @param instanceFilter instance filter object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::InstanceFilter& instanceFilter, Poco::JSON::Object& json);

/**
 * Converts JSON object to Identifier object.
 *
 * @param json json object representation.
 * @param[out] identifier identifier object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::Identifier& identifier);

/**
 * Converts Identifier object to JSON object.
 *
 * @param identifier identifier object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::Identifier& identifier, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
