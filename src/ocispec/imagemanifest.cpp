/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include "ocispec/ocispec.hpp"

#include "utils/exception.hpp"
#include "utils/json.hpp"
#include "utils/time.hpp"

namespace aos::common::oci {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

aos::oci::ContentDescriptor ContentDescriptorFromJSON(const utils::CaseInsensitiveObjectWrapper& object)
{
    const auto digest    = object.GetValue<std::string>("digest");
    const auto mediaType = object.GetValue<std::string>("mediaType");
    const auto size      = object.GetValue<uint64_t>("size");

    return {mediaType.c_str(), digest.c_str(), size};
}

Poco::JSON::Object ContentDescriptorToJSON(const aos::oci::ContentDescriptor& descriptor)
{
    Poco::JSON::Object object;

    object.set("digest", descriptor.mDigest.CStr());
    object.set("mediaType", descriptor.mMediaType.CStr());
    object.set("size", descriptor.mSize);

    return object;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error OCISpec::LoadImageManifest(const String& path, aos::oci::ImageManifest& manifest)
{
    try {
        std::ifstream file(path.CStr());

        if (!file.is_open()) {
            AOS_ERROR_THROW("failed to open file", ErrorEnum::eNotFound);
        }

        auto [var, err] = utils::ParseJson(file);
        AOS_ERROR_CHECK_AND_THROW("failed to parse json", err);

        Poco::JSON::Object::Ptr             object = var.extract<Poco::JSON::Object::Ptr>();
        utils::CaseInsensitiveObjectWrapper wrapper(object);

        manifest.mSchemaVersion = wrapper.GetValue<int>("schemaVersion");

        if (wrapper.Has("config")) {
            manifest.mConfig = ContentDescriptorFromJSON(wrapper.GetObject("config"));
        }

        if (wrapper.Has("layers")) {
            auto layers = utils::GetArrayValue<aos::oci::ContentDescriptor>(wrapper, "layers", [](const auto& value) {
                return ContentDescriptorFromJSON(utils::CaseInsensitiveObjectWrapper(value));
            });

            for (const auto& layer : layers) {
                err = manifest.mLayers.PushBack(layer);
                AOS_ERROR_CHECK_AND_THROW("layers parsing error", err);
            }
        }

        if (wrapper.Has("aosService")) {
            manifest.mAosService.SetValue(ContentDescriptorFromJSON(wrapper.GetObject("aosService")));
        }
    } catch (const utils::AosException& e) {
        return AOS_ERROR_WRAP(Error(e.GetError(), e.message().c_str()));
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, e.what()));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveImageManifest(const String& path, const aos::oci::ImageManifest& manifest)
{
    try {
        Poco::JSON::Object::Ptr object = new Poco::JSON::Object();

        object->set("schemaVersion", manifest.mSchemaVersion);
        object->set("config", ContentDescriptorToJSON(manifest.mConfig));

        if (manifest.mAosService.HasValue()) {
            object->set("aosService", ContentDescriptorToJSON(*manifest.mAosService));
        }

        Poco::JSON::Array layers;

        for (const auto& layer : manifest.mLayers) {
            layers.add(ContentDescriptorToJSON(layer));
        }

        object->set("layers", std::move(layers));

        auto err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW("failed to write json to file", err);
    } catch (const utils::AosException& e) {
        return AOS_ERROR_WRAP(Error(e.GetError(), e.message().c_str()));
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, e.what()));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::oci
