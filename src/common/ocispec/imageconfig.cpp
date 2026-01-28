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

#include "common.hpp"
#include "ocispec.hpp"

namespace aos::common::oci {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void ConfigFromJSON(const utils::CaseInsensitiveObjectWrapper& object, aos::oci::Config& config)
{
    if (object.Has("exposedPorts")) {
        for (const auto& port : object.GetObject("exposedPorts").GetNames()) {
            auto err = config.mExposedPorts.EmplaceBack(port.c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "exposedPorts parsing error");
        }
    }

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

Poco::JSON::Object ConfigToJSON(const aos::oci::Config& config)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    if (!config.mExposedPorts.IsEmpty()) {
        Poco::JSON::Object exposedPortsObj {Poco::JSON_PRESERVE_KEY_ORDER};

        for (const auto& port : config.mExposedPorts) {
            exposedPortsObj.set(port.CStr(), Poco::JSON::Object {});
        }

        object.set("exposedPorts", exposedPortsObj);
    }

    if (!config.mEnv.IsEmpty()) {
        object.set("env", utils::ToJsonArray(config.mEnv, utils::ToStdString));
    }

    if (!config.mEntryPoint.IsEmpty()) {
        object.set("entrypoint", utils::ToJsonArray(config.mEntryPoint, utils::ToStdString));
    }

    if (!config.mCmd.IsEmpty()) {
        object.set("cmd", utils::ToJsonArray(config.mCmd, utils::ToStdString));
    }

    if (!config.mWorkingDir.IsEmpty()) {
        object.set("workingDir", config.mWorkingDir.CStr());
    }

    return object;
}

void RootfsFromJSON(const utils::CaseInsensitiveObjectWrapper& object, aos::oci::Rootfs& rootfs)
{
    const auto type = object.GetValue<std::string>("type");
    rootfs.mType    = type.c_str();

    for (const auto& diffID : utils::GetArrayValue<std::string>(object, "diff_ids")) {
        auto err = rootfs.mDiffIDs.EmplaceBack(diffID.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "diff_ids parsing error");
    }
}

Poco::JSON::Object RootfsToJSON(const aos::oci::Rootfs& rootfs)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    if (!rootfs.mType.IsEmpty()) {
        object.set("type", rootfs.mType.CStr());
    }

    if (!rootfs.mDiffIDs.IsEmpty()) {
        object.set("diff_ids", utils::ToJsonArray(rootfs.mDiffIDs, utils::ToStdString));
    }

    return object;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error OCISpec::LoadImageConfig(const String& path, aos::oci::ImageConfig& imageConfig)
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

        imageConfig.mAuthor = wrapper.GetValue<std::string>("author").c_str();

        PlatformFromJSONObject(wrapper, imageConfig);

        if (const auto created = wrapper.GetOptionalValue<std::string>("created")) {
            Tie(imageConfig.mCreated, err) = utils::FromUTCString(created->c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "created time parsing error");
        }

        if (wrapper.Has("config")) {
            ConfigFromJSON(wrapper.GetObject("config"), imageConfig.mConfig);
        }

        if (wrapper.Has("rootfs")) {
            RootfsFromJSON(wrapper.GetObject("rootfs"), imageConfig.mRootfs);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveImageConfig(const String& path, const aos::oci::ImageConfig& imageConfig)
{
    try {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        if (!imageConfig.mCreated.IsZero()) {
            auto [created, err] = utils::ToUTCString(imageConfig.mCreated);
            AOS_ERROR_CHECK_AND_THROW(err, "created time parsing error");

            object->set("created", created);
        }

        if (!imageConfig.mAuthor.IsEmpty()) {
            object->set("author", imageConfig.mAuthor.CStr());
        }

        PlatformToJSONObject(imageConfig, *object);

        if (auto configObject = ConfigToJSON(imageConfig.mConfig); configObject.size() > 0) {
            object->set("config", configObject);
        }

        if (auto rootfsObject = RootfsToJSON(imageConfig.mRootfs); rootfsObject.size() > 0) {
            object->set("rootfs", rootfsObject);
        }

        auto err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to write json to file");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::oci
