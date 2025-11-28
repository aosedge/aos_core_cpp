/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "state.hpp"

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

Error ToByteArray(const std::string& str, Array<uint8_t>& dst)
{
    return String(str.c_str()).HexToByteArray(dst);
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, StateAcceptance& state)
{
    try {
        auto err = FromJSON(json, static_cast<InstanceIdent&>(state));
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse instance ident");

        err = FromJSON(json, static_cast<Protocol&>(state));
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse correlation ID");

        err = ToByteArray(json.GetValue<std::string>("checksum"), state.mChecksum);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse checksum");

        err = state.mResult.FromString(json.GetValue<std::string>("result").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse result");

        err = state.mReason.Assign(json.GetValue<std::string>("reason").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse reason");
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, UpdateState& state)
{
    try {
        auto err = FromJSON(json, static_cast<InstanceIdent&>(state));
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse instance ident");

        err = FromJSON(json, static_cast<Protocol&>(state));
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse correlation ID");

        err = ToByteArray(json.GetValue<std::string>("stateChecksum"), state.mChecksum);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse stateChecksum");

        err = state.mState.Assign(json.GetValue<std::string>("state").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse state");
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const NewState& state, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eNewState;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        auto err = ToJSON(static_cast<const Protocol&>(state), json);
        AOS_ERROR_CHECK_AND_THROW(err, "can't convert correlation ID to JSON");

        err = ToJSON(static_cast<const InstanceIdent&>(state), json);
        AOS_ERROR_CHECK_AND_THROW(err, "can't convert instance ident to JSON");

        StaticString<2 * crypto::cSHA256Size> checksumStr;

        err = checksumStr.ByteArrayToHex(state.mChecksum);
        AOS_ERROR_CHECK_AND_THROW(err, "can't convert checksum to JSON");

        json.set("stateChecksum", checksumStr.CStr());
        json.set("state", state.mState.CStr());
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const StateRequest& state, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eStateRequest;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        auto err = ToJSON(static_cast<const Protocol&>(state), json);
        AOS_ERROR_CHECK_AND_THROW(err, "can't convert correlation ID to JSON");

        err = ToJSON(static_cast<const InstanceIdent&>(state), json);
        AOS_ERROR_CHECK_AND_THROW(err, "can't convert instance ident to JSON");

        json.set("default", state.mDefault);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
