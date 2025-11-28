/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "status.hpp"

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

namespace {

constexpr auto cDefaultNackRetryAfterMillis = 500;

}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, Ack& ack)
{
    try {
        if (auto err = FromJSON(json, static_cast<Protocol&>(ack)); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const Ack& ack, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eAck;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(ack), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const Nack& nack, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eNack;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(nack), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        json.set("retryAfter", nack.mRetryAfter.Milliseconds() / Time::cMilliseconds.Milliseconds());
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, Nack& nack)
{
    try {
        const auto retryAfterMillis = json.GetValue<int64_t>("retryAfter", cDefaultNackRetryAfterMillis);
        nack.mRetryAfter            = Time::cMilliseconds * retryAfterMillis;

        if (auto err = FromJSON(json, static_cast<Protocol&>(nack)); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
