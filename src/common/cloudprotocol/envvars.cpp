/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <core/common/cloudprotocol/cloudmessage.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "envvars.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr EnvVarInfoToJSON(const aos::cloudprotocol::EnvVarInfo& envVar)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("name", envVar.mName.CStr());
    json->set("value", envVar.mValue.CStr());

    if (envVar.mTTL.HasValue()) {
        auto time = envVar.mTTL->ToUTCString();
        AOS_ERROR_CHECK_AND_THROW(time.mError, "failed to convert TTL to UTC string");

        json->set("ttl", time.mValue.CStr());
    }

    return json;
}

void EnvVarInfoFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::EnvVarInfo& envVar)
{
    auto err = envVar.mName.Assign(json.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse environment variable name");

    err = envVar.mValue.Assign(json.GetValue<std::string>("value").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse environment variable value");

    if (json.Has("ttl")) {
        envVar.mTTL.EmplaceValue();

        Tie(envVar.mTTL.GetValue(), err) = Time::UTC(json.GetValue<std::string>("ttl").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse environment variable TTL from JSON");
    }
}

Poco::JSON::Object::Ptr EnvVarsInstanceInfoToJSON(const aos::cloudprotocol::EnvVarsInstanceInfo& envVar)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(envVar.mFilter, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert filter to JSON");

    json->set("variables", utils::ToJsonArray(envVar.mVariables, EnvVarInfoToJSON));

    return json;
}

void EnvVarsInstanceInfoFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::EnvVarsInstanceInfo& envVar)
{
    auto err = FromJSON(json, envVar.mFilter);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert JSON to filter");

    utils::ForEach(json, "variables", [&envVar](const auto& item) {
        auto err = envVar.mVariables.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse environment variable");

        EnvVarInfoFromJSON(utils::CaseInsensitiveObjectWrapper(item), envVar.mVariables.Back());
    });
}

Poco::JSON::Object::Ptr EnvVarStatusToJSON(const aos::cloudprotocol::EnvVarStatus& envVar)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("name", envVar.mName.CStr());

    if (!envVar.mError.IsNone()) {
        auto errorInfo = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(envVar.mError, *errorInfo);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error info to JSON");

        json->set("errorInfo", errorInfo);
    }

    return json;
}

void EnvVarStatusFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::EnvVarStatus& envVar)
{
    auto err = envVar.mName.Assign(json.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to unparse environment variable name");

    if (json.Has("errorInfo")) {
        err = FromJSON(utils::CaseInsensitiveObjectWrapper(json.GetObject("errorInfo")), envVar.mError);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse errorInfo from JSON");
    }
}

Poco::JSON::Object::Ptr EnvVarsInstanceStatusToJSON(const aos::cloudprotocol::EnvVarsInstanceStatus& envVar)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(envVar.mFilter, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert filter to JSON");

    json->set("statuses", utils::ToJsonArray(envVar.mStatuses, EnvVarStatusToJSON));

    return json;
}

void EnvVarsInstanceStatusFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::EnvVarsInstanceStatus& envVar)
{
    auto err = FromJSON(json, envVar.mFilter);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert JSON to filter");

    utils::ForEach(json, "statuses", [&envVar](const auto& item) {
        auto err = envVar.mStatuses.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse environment variable status");

        EnvVarStatusFromJSON(utils::CaseInsensitiveObjectWrapper(item), envVar.mStatuses.Back());
    });
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::OverrideEnvVarsRequest& envVars)
{
    try {
        utils::ForEach(json, "items", [&envVars](const auto& item) {
            auto err = envVars.mItems.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse environment variable");

            EnvVarsInstanceInfoFromJSON(utils::CaseInsensitiveObjectWrapper(item), envVars.mItems.Back());
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::OverrideEnvVarsRequest& envVars, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType = aos::cloudprotocol::MessageTypeEnum::eOverrideEnvVars;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("items", utils::ToJsonArray(envVars.mItems, EnvVarsInstanceInfoToJSON));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::OverrideEnvVarsStatuses& envVars)
{
    try {
        utils::ForEach(json, "statuses", [&envVars](const auto& item) {
            auto err = envVars.mStatuses.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse environment variable");

            EnvVarsInstanceStatusFromJSON(utils::CaseInsensitiveObjectWrapper(item), envVars.mStatuses.Back());
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::OverrideEnvVarsStatuses& envVars, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType
        = aos::cloudprotocol::MessageTypeEnum::eOverrideEnvVarsStatus;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("statuses", utils::ToJsonArray(envVars.mStatuses, EnvVarsInstanceStatusToJSON));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
