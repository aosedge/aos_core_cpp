/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_CLOUDPROTOCOL_STATE_HPP_
#define AOS_CM_COMMUNICATION_CLOUDPROTOCOL_STATE_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/state.hpp>

#include <common/utils/json.hpp>

namespace aos::cm::communication::cloudprotocol {

/**
 * Converts JSON object to state acceptance.
 *
 * @param json JSON object to parse.
 * @param[out] state object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, StateAcceptance& state);

/**
 * Converts JSON object to update state.
 *
 * @param json JSON object to parse.
 * @param[out] state object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, UpdateState& state);

/**
 * Converts new state to JSON object.
 *
 * @param state object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const NewState& state, Poco::JSON::Object& json);

/**
 * Converts state request to JSON object.
 *
 * @param state object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const StateRequest& state, Poco::JSON::Object& json);

} // namespace aos::cm::communication::cloudprotocol

#endif
