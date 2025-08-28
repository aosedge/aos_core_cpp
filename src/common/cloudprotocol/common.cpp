/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"

namespace aos::common::cloudprotocol {

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, Error& error)
{
    if (auto code = json.GetValue<int>("aosCode", 0); code > 0) {
        error = Error(static_cast<Error::Enum>(code), json.GetValue<std::string>("message", "").c_str());
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const Error& error, Poco::JSON::Object& json)
{
    json.set("aosCode", static_cast<int>(error.Value()));
    json.set("errno", error.Errno());
    json.set("message", error.Message());

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, InstanceIdent& instanceIdent)
{
    if (auto err = instanceIdent.mServiceID.Assign(json.GetValue<std::string>("serviceID").c_str()); !err.IsNone()) {
        return Error(err, "serviceID parsing failed");
    }

    if (auto err = instanceIdent.mSubjectID.Assign(json.GetValue<std::string>("subjectID").c_str()); !err.IsNone()) {
        return Error(err, "subjectID parsing failed");
    }

    instanceIdent.mInstance = json.GetValue<uint64_t>("instance");

    return ErrorEnum::eNone;
}

Error ToJSON(const InstanceIdent& instanceIdent, Poco::JSON::Object& json)
{
    json.set("serviceID", instanceIdent.mServiceID.CStr());
    json.set("subjectID", instanceIdent.mSubjectID.CStr());
    json.set("instance", instanceIdent.mInstance);

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::InstanceFilter& instanceFilter)
{
    if (json.Has("serviceID")) {
        instanceFilter.mServiceID.EmplaceValue();

        if (auto err = instanceFilter.mServiceID.GetValue().Assign(json.GetValue<std::string>("serviceID").c_str());
            !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "failed to parse serviceID"));
        }
    }

    if (json.Has("subjectID")) {
        instanceFilter.mSubjectID.EmplaceValue();

        if (auto err = instanceFilter.mSubjectID.GetValue().Assign(json.GetValue<std::string>("subjectID").c_str());
            !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "failed to parse subjectID"));
        }
    }

    if (json.Has("instance")) {
        instanceFilter.mInstance.EmplaceValue(json.GetValue<uint64_t>("instance"));
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::InstanceFilter& instanceFilter, Poco::JSON::Object& json)
{
    try {
        if (instanceFilter.mServiceID.HasValue()) {
            json.set("serviceID", instanceFilter.mServiceID.GetValue().CStr());
        }

        if (instanceFilter.mSubjectID.HasValue()) {
            json.set("subjectID", instanceFilter.mSubjectID.GetValue().CStr());
        }

        if (instanceFilter.mInstance.HasValue()) {
            json.set("instance", instanceFilter.mInstance.GetValue());
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::Identifier& identifier)
{
    if (json.Has("id")) {
        auto [id, err] = uuid::StringToUUID(json.GetValue<std::string>("id").c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't parse id"));
        }

        identifier.mID.EmplaceValue(id);
    }

    if (json.Has("type")) {
        UpdateItemType type;

        auto err = type.FromString(json.GetValue<std::string>("type").c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "wrong type"));
        }

        identifier.mType.EmplaceValue(type);
    }

    if (json.Has("codename")) {
        identifier.mCodeName.EmplaceValue();

        auto err = identifier.mCodeName->Assign(json.GetValue<std::string>("codename").c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "failed to parse codename"));
        }
    }

    if (json.Has("title")) {
        identifier.mTitle.EmplaceValue();

        auto err = identifier.mTitle->Assign(json.GetValue<std::string>("title").c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "failed to parse title"));
        }
    }

    if (json.Has("description")) {
        identifier.mDescription.EmplaceValue();

        auto err = identifier.mDescription->Assign(json.GetValue<std::string>("description").c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "failed to parse description"));
        }
    }

    if (json.Has("urn")) {
        identifier.mURN.EmplaceValue();

        auto err = identifier.mURN->Assign(json.GetValue<std::string>("urn").c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "failed to parse URN"));
        }
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::Identifier& identifier, Poco::JSON::Object& json)
{
    try {
        if (identifier.mID.HasValue()) {
            json.set("serviceID", uuid::UUIDToString(*identifier.mID).CStr());
        }

        if (identifier.mType.HasValue()) {
            json.set("type", identifier.mType->ToString().CStr());
        }

        if (identifier.mCodeName.HasValue()) {
            json.set("codename", identifier.mCodeName->CStr());
        }

        if (identifier.mTitle.HasValue()) {
            json.set("title", identifier.mTitle->CStr());
        }

        if (identifier.mDescription.HasValue()) {
            json.set("description", identifier.mDescription->CStr());
        }

        if (identifier.mURN.HasValue()) {
            json.set("urn", identifier.mURN->CStr());
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
