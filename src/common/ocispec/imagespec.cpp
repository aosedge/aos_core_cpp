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
 * Static
 **********************************************************************************************************************/

namespace {

std::string ToStdString(const String& str)
{
    return str.CStr();
}

void ImageConfigFromJSON(const utils::CaseInsensitiveObjectWrapper& object, aos::oci::ImageConfig& config)
{
    for (const auto& env : utils::GetArrayValue<std::string>(object, "env")) {
        auto err = config.mEnv.EmplaceBack(env.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "env parsing error");
    }

    for (const auto& entrypoint : utils::GetArrayValue<std::string>(object, "entrypoint")) {
        auto err = config.mEntryPoint.EmplaceBack(entrypoint.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "entrypoint parsing error");
    }

    for (const auto& cmd : utils::GetArrayValue<std::string>(object, "cmd")) {
        auto err = config.mCmd.EmplaceBack(cmd.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "cmd parsing error");
    }

    const auto workingDir = object.GetValue<std::string>("workingDir");
    config.mWorkingDir    = workingDir.c_str();
}

void ImageSpecFromJSONObject(const utils::CaseInsensitiveObjectWrapper& wrapper, aos::oci::ImageSpec& imageSpec)
{
    if (wrapper.Has("config")) {
        ImageConfigFromJSON(wrapper.GetObject("config"), imageSpec.mConfig);
    }

    const auto author       = wrapper.GetValue<std::string>("author");
    const auto architecture = wrapper.GetValue<std::string>("architecture");
    const auto os           = wrapper.GetValue<std::string>("os");
    const auto osVersion    = wrapper.GetValue<std::string>("osVersion");
    const auto variant      = wrapper.GetValue<std::string>("variant");

    imageSpec.mAuthor       = author.c_str();
    imageSpec.mArchitecture = architecture.c_str();
    imageSpec.mOS           = os.c_str();
    imageSpec.mOSVersion    = osVersion.c_str();
    imageSpec.mVariant      = variant.c_str();

    if (const auto created = wrapper.GetOptionalValue<std::string>("created"); created.has_value()) {
        Error err;
        // cppcheck-suppress unusedScopedObject
        Tie(imageSpec.mCreated, err) = utils::FromUTCString(created->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "created time parsing error");
    }
}

Poco::JSON::Object ImageConfigToJSON(const aos::oci::ImageConfig& config)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    if (!config.mEnv.IsEmpty()) {
        object.set("env", utils::ToJsonArray(config.mEnv, ToStdString));
    }

    if (!config.mEntryPoint.IsEmpty()) {
        object.set("entrypoint", utils::ToJsonArray(config.mEntryPoint, ToStdString));
    }

    if (!config.mCmd.IsEmpty()) {
        object.set("cmd", utils::ToJsonArray(config.mCmd, ToStdString));
    }

    if (!config.mWorkingDir.IsEmpty()) {
        object.set("workingDir", config.mWorkingDir.CStr());
    }

    return object;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error OCISpec::ImageSpecFromFile(const String& path, aos::oci::ImageSpec& imageSpec)
{
    try {
        std::ifstream file(path.CStr());

        if (!file.is_open()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "failed to open file");
        }

        auto [var, err] = utils::ParseJson(file);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse json");

        Poco::JSON::Object::Ptr             object = var.extract<Poco::JSON::Object::Ptr>();
        utils::CaseInsensitiveObjectWrapper wrapper(object);

        ImageSpecFromJSONObject(wrapper, imageSpec);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::ImageSpecFromJSON(const String& json, aos::oci::ImageSpec& imageSpec)
{
    try {
        std::istringstream stream(json.CStr());

        auto [var, err] = utils::ParseJson(stream);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse json");

        Poco::JSON::Object::Ptr             object = var.extract<Poco::JSON::Object::Ptr>();
        utils::CaseInsensitiveObjectWrapper wrapper(object);

        ImageSpecFromJSONObject(wrapper, imageSpec);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveImageSpec(const String& path, const aos::oci::ImageSpec& imageSpec)
{
    try {
        Poco::JSON::Object::Ptr object = new Poco::JSON::Object(Poco::JSON_PRESERVE_KEY_ORDER);

        if (!imageSpec.mCreated.IsZero()) {
            auto [created, err] = utils::ToUTCString(imageSpec.mCreated);
            AOS_ERROR_CHECK_AND_THROW(err, "created time parsing error");

            object->set("created", created);
        }

        if (!imageSpec.mAuthor.IsEmpty()) {
            object->set("author", imageSpec.mAuthor.CStr());
        }

        object->set("architecture", imageSpec.mArchitecture.CStr());
        object->set("os", imageSpec.mOS.CStr());

        if (!imageSpec.mOSVersion.IsEmpty()) {
            object->set("osVersion", imageSpec.mOSVersion.CStr());
        }

        if (!imageSpec.mVariant.IsEmpty()) {
            object->set("variant", imageSpec.mVariant.CStr());
        }

        if (auto configObject = ImageConfigToJSON(imageSpec.mConfig); configObject.size() > 0) {
            object->set("config", configObject);
        }

        auto err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to write json to file");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::oci
