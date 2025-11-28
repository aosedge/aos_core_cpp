/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_CLOUDPROTOCOL_STATUS_HPP_
#define AOS_CM_COMMUNICATION_CLOUDPROTOCOL_STATUS_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/common.hpp>

#include <common/utils/json.hpp>

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/**
 * Acknowledgment message.
 */
struct Ack : public Protocol { };

/**
 * Negative acknowledgment message.
 */
struct Nack : public Protocol {
    Duration mRetryAfter {};
};

/**
 * Converts JSON object to acknowledgment message.
 *
 * @param json JSON object to parse.
 * @param[out] ack object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, Ack& ack);

/**
 * Converts acknowledgment message to JSON object.
 *
 * @param ack acknowledgment message.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const Ack& ack, Poco::JSON::Object& json);

/**
 * Converts nack message to JSON object.
 *
 * @param nack nack message.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const Nack& nack, Poco::JSON::Object& json);

/**
 * Converts JSON object to nack message.
 *
 * @param json JSON object to parse.
 * @param[out] nack object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, Nack& nack);

} // namespace aos::cm::communication::cloudprotocol

#endif
