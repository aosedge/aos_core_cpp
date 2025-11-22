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

void IndexContentDescriptorFromJSONObject(
    const utils::CaseInsensitiveObjectWrapper& object, aos::oci::IndexContentDescriptor& descriptor)
{
    ContentDescriptorFromJSONObject(object, descriptor);

    if (object.Has("platform")) {
        descriptor.mPlatform.EmplaceValue();
        PlatformFromJSONObject(object.GetObject("platform"), *descriptor.mPlatform);
    }
}

Poco::JSON::Object IndexContentDescriptorToJSONObject(const aos::oci::IndexContentDescriptor& descriptor)
{
    auto object = ContentDescriptorToJSONObject(descriptor);

    if (descriptor.mPlatform.HasValue()) {
        Poco::JSON::Object platformObject {Poco::JSON_PRESERVE_KEY_ORDER};

        PlatformToJSONObject(*descriptor.mPlatform, platformObject);
        object.set("platform", platformObject);
    }

    return object;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error OCISpec::LoadImageIndex(const String& path, aos::oci::ImageIndex& index)
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

        index.mSchemaVersion = wrapper.GetValue<int>("schemaVersion");

        err = index.mMediaType.Assign(wrapper.GetValue<std::string>("mediaType").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse mediaType");

        if (auto artifactType = wrapper.GetOptionalValue<std::string>("artifactType")) {
            err = index.mArtifactType.Assign(artifactType->c_str());
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse artifactType");
        }

        if (wrapper.Has("manifests")) {
            auto manifests
                = utils::GetArrayValue<aos::oci::IndexContentDescriptor>(wrapper, "manifests", [](const auto& value) {
                      aos::oci::IndexContentDescriptor descriptor;

                      IndexContentDescriptorFromJSONObject(utils::CaseInsensitiveObjectWrapper(value), descriptor);

                      return descriptor;
                  });

            for (const auto& manifest : manifests) {
                err = index.mManifests.PushBack(manifest);
                AOS_ERROR_CHECK_AND_THROW(err, "manifests parsing error");
            }
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error OCISpec::SaveImageIndex(const String& path, const aos::oci::ImageIndex& index)
{
    try {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("schemaVersion", index.mSchemaVersion);
        object->set("mediaType", index.mMediaType.CStr());

        if (!index.mArtifactType.IsEmpty()) {
            object->set("artifactType", index.mArtifactType.CStr());
        }

        if (!index.mManifests.IsEmpty()) {
            Poco::JSON::Array manifests;

            for (const auto& manifest : index.mManifests) {
                manifests.add(IndexContentDescriptorToJSONObject(manifest));
            }

            object->set("manifests", std::move(manifests));
        }

        auto err = utils::WriteJsonToFile(object, path.CStr());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to write json to file");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::oci
