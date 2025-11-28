/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "envvars.hpp"

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void EnvVarInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, EnvVarInfo& envVar)
{
    auto err = envVar.mName.Assign(json.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse environment variable name");

    err = envVar.mValue.Assign(json.GetValue<std::string>("value").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse environment variable value");

    if (json.Has("ttl")) {
        envVar.mTTL.EmplaceValue();

        Tie(envVar.mTTL.GetValue(), err) = Time::UTC(json.GetValue<std::string>("ttl").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse environment variable TTL");
    }
}

void EnvVarsInstanceInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, EnvVarsInstanceInfo& envVar)
{
    auto err = FromJSON(json, static_cast<InstanceFilter&>(envVar));
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse instance filter");

    common::utils::ForEach(json, "variables", [&envVar](const auto& item) {
        auto err = envVar.mVariables.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse environment variable");

        EnvVarInfoFromJSON(common::utils::CaseInsensitiveObjectWrapper(item), envVar.mVariables.Back());
    });
}

void EnvVarsInstanceStatusToJSON(const EnvVarsInstanceStatus& status, Poco::JSON::Array& json)
{
    auto instanceIdentJSON = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(static_cast<const InstanceIdent&>(status), *instanceIdentJSON);
    AOS_ERROR_CHECK_AND_THROW(err, "can't convert instance ident to JSON");

    for (const auto& envVar : status.mStatuses) {
        auto item = Poco::makeShared<Poco::JSON::Object>(*instanceIdentJSON);

        item->set("name", envVar.mName.CStr());
        if (!envVar.mError.IsNone()) {
            auto errorJSON = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            err = ToJSON(envVar.mError, *errorJSON);
            AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

            item->set("errorInfo", errorJSON);
        }

        json.add(item);
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, OverrideEnvVarsRequest& envVars)
{
    try {
        if (auto err = FromJSON(json, static_cast<Protocol&>(envVars)); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        common::utils::ForEach(json, "items", [&envVars](const auto& item) {
            auto err = envVars.mItems.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse environment variable");

            EnvVarsInstanceInfoFromJSON(common::utils::CaseInsensitiveObjectWrapper(item), envVars.mItems.Back());
        });
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const OverrideEnvVarsStatuses& envVars, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eOverrideEnvVarsStatus;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(envVars), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto statuses = Poco::makeShared<Poco::JSON::Array>(Poco::JSON_PRESERVE_KEY_ORDER);

        for (const auto& status : envVars.mStatuses) {
            EnvVarsInstanceStatusToJSON(status, *statuses);
        }

        json.set("statuses", statuses);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
