/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <sstream>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "ocispec.hpp"

namespace aos::common::oci {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error OCISpec::LoadComponentConfig(const String& path, aos::oci::ComponentConfig& componentConfig)
{
    try {
        std::ifstream file(path.CStr());

        if (!file.is_open()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "failed to open file");
        }

        auto [var, err] = utils::ParseJson(file);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse json");

        utils::CaseInsensitiveObjectWrapper wrapper(var.extract<Poco::JSON::Object::Ptr>());

        if (const auto created = wrapper.GetOptionalValue<std::string>("created")) {
            Tie(componentConfig.mCreated, err) = Time::UTC(created->c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "created time parsing error");
        }

        const auto author       = wrapper.GetValue<std::string>("author");
        componentConfig.mAuthor = author.c_str();

        if (!wrapper.Has("runner")) {
            AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "runner field is missing");
        }

        const auto runner       = wrapper.GetValue<std::string>("runner");
        componentConfig.mRunner = runner.c_str();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveComponentConfig(const String& path, const aos::oci::ComponentConfig& componentConfig)
{
    try {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto [created, err] = componentConfig.mCreated.ToUTCString();
        AOS_ERROR_CHECK_AND_THROW(err, "created time parsing error");

        object->set("created", created.CStr());
        object->set("author", componentConfig.mAuthor.CStr());
        object->set("runner", componentConfig.mRunner.CStr());

        err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to write json to file");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::oci
