/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>
#include <core/common/tools/logger.hpp>

#include "common.hpp"

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, Error& error)
{
    if (auto code = json.GetValue<int>("aosCode", 0); code > 0) {
        error = Error(static_cast<Error::Enum>(code), json.GetValue<std::string>("message", "").c_str());
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const Error& error, Poco::JSON::Object& json)
{
    json.set("aosCode", static_cast<int>(error.Value()));
    json.set("exitCode", error.Errno());
    json.set("message", error.Message());

    return ErrorEnum::eNone;
}

Poco::JSON::Object::Ptr CreateAosIdentity(const AosIdentity& identity)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (identity.mID.has_value()) {
        json->set("id", *identity.mID);
    }

    if (identity.mType.has_value()) {
        json->set("type", identity.mType->ToString().CStr());
    }

    if (identity.mCodename.has_value()) {
        json->set("codename", *identity.mCodename);
    }

    if (identity.mTitle.has_value()) {
        json->set("title", *identity.mTitle);
    }

    return json;
}

Error ParseAosIdentity(const common::utils::CaseInsensitiveObjectWrapper& json, AosIdentity& identity)
{
    try {
        identity.mID       = json.GetOptionalValue<std::string>("id");
        identity.mCodename = json.GetOptionalValue<std::string>("codename");
        identity.mTitle    = json.GetOptionalValue<std::string>("title");

        if (const auto type = json.GetOptionalValue<std::string>("type"); type.has_value()) {
            identity.mType.emplace();

            if (auto err = identity.mType->FromString(type->c_str()); !err.IsNone()) {
                LOG_WRN() << "Failed to parse AosIdentity type" << Log::Field("type", type->c_str()) << Log::Field(err);

                identity.mType.reset();
            }
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const InstanceIdent& instanceIdent, Poco::JSON::Object& json)
{
    try {
        {
            AosIdentity identity;

            if (instanceIdent.mPreinstalled) {
                identity.mCodename = instanceIdent.mItemID.CStr();
            } else {
                identity.mID = instanceIdent.mItemID.CStr();
            }

            json.set("item", CreateAosIdentity(identity));
        }

        {
            AosIdentity identity;

            if (instanceIdent.mPreinstalled) {
                identity.mCodename = instanceIdent.mSubjectID.CStr();
            } else {
                identity.mID = instanceIdent.mSubjectID.CStr();
            }

            json.set("subject", CreateAosIdentity(identity));
        }

        json.set("instance", instanceIdent.mInstance);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, InstanceIdent& instanceIdent)
{
    try {
        {
            AosIdentity identity;

            auto err = ParseAosIdentity(json.GetObject("item"), identity);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse item identity");

            err = instanceIdent.mItemID.Assign(identity.mID.value_or("").c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse item ID");
        }

        {
            AosIdentity identity;

            auto err = ParseAosIdentity(json.GetObject("subject"), identity);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject identity");

            err = instanceIdent.mSubjectID.Assign(identity.mID.value_or("").c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject ID");
        }

        instanceIdent.mInstance = json.GetValue<uint64_t>("instance", 0);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, InstanceFilter& instanceFilter)
{
    try {
        StaticString<cIDLen> id;

        if (json.Has("item")) {
            AosIdentity identity;

            auto err = ParseAosIdentity(json.GetObject("item"), identity);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

            instanceFilter.mItemID.SetValue(identity.mID.value_or("").c_str());
        }

        if (json.Has("subject")) {
            AosIdentity identity;

            auto err = ParseAosIdentity(json.GetObject("subject"), identity);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject");

            instanceFilter.mSubjectID.SetValue(identity.mID.value_or("").c_str());
        }

        if (json.Has("instance")) {
            instanceFilter.mInstance.SetValue(json.GetValue<uint64_t>("instance", 0));
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const Protocol& protocol, Poco::JSON::Object& json)
{
    try {
        if (!protocol.mCorrelationID.IsEmpty()) {
            json.set("correlationId", protocol.mCorrelationID.CStr());
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, Protocol& protocol)
{
    if (auto err = protocol.mCorrelationID.Assign(json.GetValue<std::string>("correlationId").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "can't parse correlationId"));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
