/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_STATE_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_STATE_HPP_

#include <variant>

#include <Poco/JSON/Object.h>

#include <aos/common/cloudprotocol/state.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to state acceptance.
 *
 * @param json JSON object to parse.
 * @param[out] state object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::StateAcceptance& state);

/**
 * Converts state acceptance to JSON object.
 *
 * @param state object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::StateAcceptance& state, Poco::JSON::Object& json);

/**
 * Converts JSON object to update state.
 *
 * @param json JSON object to parse.
 * @param[out] state object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::UpdateState& state);

/**
 * Converts update state to JSON object.
 *
 * @param state object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::UpdateState& state, Poco::JSON::Object& json);

/**
 * Converts JSON object to new state.
 *
 * @param json JSON object to parse.
 * @param[out] state object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::NewState& state);

/**
 * Converts new state to JSON object.
 *
 * @param state object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::NewState& state, Poco::JSON::Object& json);

/**
 * Converts JSON object to state request.
 *
 * @param json JSON object to parse.
 * @param[out] state object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::StateRequest& state);

/**
 * Converts state request to JSON object.
 *
 * @param state object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::StateRequest& state, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
