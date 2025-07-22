/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "cloudmessage.hpp"
#include "common.hpp"
#include "unitstatus.hpp"

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::StateAcceptance& state)
{
    try {
        FromJSON(json, state.mInstanceIdent);

        auto err = state.mChecksum.Assign(json.GetValue<std::string>("checksum").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "checksum parsing failed");

        err = state.mResult.FromString(json.GetValue<std::string>("result").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "result parsing failed");

        err = state.mReason.Assign(json.GetValue<std::string>("reason").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "reason parsing failed");
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::StateAcceptance& state, Poco::JSON::Object& json)
{
    try {
        json.set("messageType", state.mMessageType.ToString().CStr());

        auto err = ToJSON(state.mInstanceIdent, json);
        AOS_ERROR_CHECK_AND_THROW(err);

        json.set("checksum", state.mChecksum.CStr());
        json.set("result", state.mResult.ToString().CStr());
        json.set("reason", state.mReason.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::UpdateState& state)
{
    try {
        FromJSON(json, state.mInstanceIdent);

        auto err = state.mChecksum.Assign(json.GetValue<std::string>("stateChecksum").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "checksum parsing failed");

        err = state.mState.Assign(json.GetValue<std::string>("state").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "state parsing failed");
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::UpdateState& state, Poco::JSON::Object& json)
{
    try {
        json.set("messageType", state.mMessageType.ToString().CStr());

        auto err = ToJSON(state.mInstanceIdent, json);
        AOS_ERROR_CHECK_AND_THROW(err);

        json.set("stateChecksum", state.mChecksum.CStr());
        json.set("state", state.mState.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::NewState& state)
{
    try {
        FromJSON(json, state.mInstanceIdent);

        auto err = state.mChecksum.Assign(json.GetValue<std::string>("stateChecksum").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "checksum parsing failed");

        err = state.mState.Assign(json.GetValue<std::string>("state").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "state parsing failed");
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::NewState& state, Poco::JSON::Object& json)
{
    try {
        json.set("messageType", state.mMessageType.ToString().CStr());

        auto err = ToJSON(state.mInstanceIdent, json);
        AOS_ERROR_CHECK_AND_THROW(err);

        json.set("stateChecksum", state.mChecksum.CStr());
        json.set("state", state.mState.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::StateRequest& state)
{
    try {
        FromJSON(json, state.mInstanceIdent);

        state.mDefault = json.GetValue<bool>("default", false);
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::StateRequest& state, Poco::JSON::Object& json)
{
    try {
        json.set("messageType", state.mMessageType.ToString().CStr());

        auto err = ToJSON(state.mInstanceIdent, json);
        AOS_ERROR_CHECK_AND_THROW(err);

        json.set("default", state.mDefault);
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
