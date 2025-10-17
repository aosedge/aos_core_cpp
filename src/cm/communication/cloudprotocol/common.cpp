/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

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

Poco::JSON::Object::Ptr CreateAosIdentity(const Optional<String>& id, const Optional<UpdateItemType>& type)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (id.HasValue()) {
        json->set("id", id->CStr());
    }

    if (type.HasValue()) {
        json->set("type", type->ToString().CStr());
    }

    return json;
}

Error ParseAosIdentityID(const common::utils::CaseInsensitiveObjectWrapper& json, String& id)
{
    try {
        if (!json.Has("id")) {
            AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "missing id tag");
        }

        auto err = id.Assign(json.GetValue<std::string>("id").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse id");
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const InstanceIdent& instanceIdent, Poco::JSON::Object& json)
{
    try {
        json.set("item", CreateAosIdentity({instanceIdent.mItemID}));
        json.set("subject", CreateAosIdentity({instanceIdent.mSubjectID}));
        json.set("instance", instanceIdent.mInstance);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, InstanceIdent& instanceIdent)
{
    try {
        if (!json.Has("item")) {
            AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "missing item tag");
        }

        auto err = ParseAosIdentityID(json.GetObject("item"), instanceIdent.mItemID);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

        if (!json.Has("subject")) {
            AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "missing subject tag");
        }

        err = ParseAosIdentityID(json.GetObject("subject"), instanceIdent.mSubjectID);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject");

        if (!json.Has("instance")) {
            AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "missing instance tag");
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
            auto err = ParseAosIdentityID(json.GetObject("item"), id);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse item");

            instanceFilter.mItemID.SetValue(id);
        }

        if (json.Has("subject")) {
            auto err = ParseAosIdentityID(json.GetObject("subject"), id);
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse subject");

            instanceFilter.mSubjectID.SetValue(id);
        }

        if (json.Has("instance")) {
            instanceFilter.mInstance.SetValue(json.GetValue<uint64_t>("instance", 0));
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
