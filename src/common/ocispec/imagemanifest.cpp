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

void ContentDescriptorFromJSONObject(
    const utils::CaseInsensitiveObjectWrapper& object, aos::oci::ContentDescriptor& descriptor)
{
    const auto mediaType = object.GetValue<std::string>("mediaType");
    const auto digest    = object.GetValue<std::string>("digest");
    const auto size      = object.GetValue<uint64_t>("size");

    descriptor.mMediaType = mediaType.c_str();
    descriptor.mDigest    = digest.c_str();
    descriptor.mSize      = size;
}

Poco::JSON::Object ContentDescriptorToJSONObject(const aos::oci::ContentDescriptor& descriptor)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    object.set("mediaType", descriptor.mMediaType.CStr());
    object.set("digest", descriptor.mDigest.CStr());
    object.set("size", descriptor.mSize);

    return object;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error OCISpec::ContentDescriptorFromFile(const String& path, aos::oci::ContentDescriptor& descriptor)
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

        ContentDescriptorFromJSONObject(wrapper, descriptor);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::ContentDescriptorFromJSON(const String& json, aos::oci::ContentDescriptor& descriptor)
{
    try {
        std::istringstream stream(json.CStr());

        auto [var, err] = utils::ParseJson(stream);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse json");

        Poco::JSON::Object::Ptr             object = var.extract<Poco::JSON::Object::Ptr>();
        utils::CaseInsensitiveObjectWrapper wrapper(object);

        ContentDescriptorFromJSONObject(wrapper, descriptor);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveContentDescriptor(const String& path, const aos::oci::ContentDescriptor& descriptor)
{
    try {
        Poco::JSON::Object::Ptr object = new Poco::JSON::Object(ContentDescriptorToJSONObject(descriptor));

        auto err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to write json to file");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::LoadImageManifest(const String& path, aos::oci::ImageManifest& manifest)
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

        manifest.mSchemaVersion = wrapper.GetValue<int>("schemaVersion");

        if (wrapper.Has("config")) {
            ContentDescriptorFromJSONObject(wrapper.GetObject("config"), manifest.mConfig);
        }

        if (wrapper.Has("layers")) {
            auto layers = utils::GetArrayValue<aos::oci::ContentDescriptor>(wrapper, "layers", [](const auto& value) {
                aos::oci::ContentDescriptor descriptor;

                ContentDescriptorFromJSONObject(utils::CaseInsensitiveObjectWrapper(value), descriptor);

                return descriptor;
            });

            for (const auto& layer : layers) {
                err = manifest.mLayers.PushBack(layer);
                AOS_ERROR_CHECK_AND_THROW(err, "layers parsing error");
            }
        }

        if (wrapper.Has("aosService")) {
            manifest.mAosService.SetValue({});

            ContentDescriptorFromJSONObject(wrapper.GetObject("aosService"), *manifest.mAosService);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveImageManifest(const String& path, const aos::oci::ImageManifest& manifest)
{
    try {
        Poco::JSON::Object::Ptr object = new Poco::JSON::Object(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("schemaVersion", manifest.mSchemaVersion);
        object->set("config", ContentDescriptorToJSONObject(manifest.mConfig));

        if (manifest.mAosService.HasValue()) {
            object->set("aosService", ContentDescriptorToJSONObject(*manifest.mAosService));
        }

        if (!manifest.mLayers.IsEmpty()) {
            Poco::JSON::Array layers;

            for (const auto& layer : manifest.mLayers) {
                layers.add(ContentDescriptorToJSONObject(layer));
            }

            object->set("layers", std::move(layers));
        }

        auto err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to write json to file");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::oci
