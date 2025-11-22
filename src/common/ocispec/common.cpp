/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <common/utils/exception.hpp>

#include "common.hpp"

namespace aos::common::oci {

void ContentDescriptorFromJSONObject(
    const utils::CaseInsensitiveObjectWrapper& object, aos::oci::ContentDescriptor& descriptor)
{
    auto err = descriptor.mMediaType.Assign(object.GetValue<std::string>("mediaType").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse mediaType");

    err = descriptor.mDigest.Assign(object.GetValue<std::string>("digest").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse digest");

    descriptor.mSize = object.GetValue<uint64_t>("size");
}

Poco::JSON::Object ContentDescriptorToJSONObject(const aos::oci::ContentDescriptor& descriptor)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    object.set("mediaType", descriptor.mMediaType.CStr());
    object.set("digest", descriptor.mDigest.CStr());
    object.set("size", descriptor.mSize);

    return object;
}

void PlatformFromJSONObject(const utils::CaseInsensitiveObjectWrapper& object, aos::oci::Platform& platform)
{
    auto err = platform.mArchitecture.Assign(object.GetValue<std::string>("architecture").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "architecture parsing error");

    if (auto variant = object.GetOptionalValue<std::string>("variant")) {
        err = platform.mVariant.Assign(variant->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "variant parsing error");
    }

    err = platform.mOS.Assign(object.GetValue<std::string>("os").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "os parsing error");

    if (auto osVersion = object.GetOptionalValue<std::string>("os.version")) {
        err = platform.mOSVersion.Assign(osVersion->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "os.version parsing error");
    }

    for (const auto& feature : utils::GetArrayValue<std::string>(object, "os.features")) {
        err = platform.mOSFeatures.PushBack(feature.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "features parsing error");
    }
}

void PlatformToJSONObject(const aos::oci::Platform& platform, Poco::JSON::Object& object)
{
    object.set("architecture", platform.mArchitecture.CStr());

    if (!platform.mVariant.IsEmpty()) {
        object.set("variant", platform.mVariant.CStr());
    }

    object.set("os", platform.mOS.CStr());

    if (!platform.mOSVersion.IsEmpty()) {
        object.set("os.version", platform.mOSVersion.CStr());
    }

    if (!platform.mOSFeatures.IsEmpty()) {
        object.set("os.features", utils::ToJsonArray(platform.mOSFeatures, utils::ToStdString));
    }
}

} // namespace aos::common::oci
