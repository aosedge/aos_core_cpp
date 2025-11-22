/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_OCISPEC_COMMON_HPP_
#define AOS_COMMON_OCISPEC_COMMON_HPP_

#include <core/common/ocispec/itf/imagespec.hpp>

#include <common/utils/json.hpp>

namespace aos::common::oci {

/**
 * Converts content descriptor from JSON object.
 *
 * @param object JSON object.
 * @param descriptor content descriptor.
 */
void ContentDescriptorFromJSONObject(
    const utils::CaseInsensitiveObjectWrapper& object, aos::oci::ContentDescriptor& descriptor);

/**
 * Converts content descriptor to JSON object.
 *
 * @param descriptor content descriptor.
 * @return Poco::JSON::Object JSON object.
 */
Poco::JSON::Object ContentDescriptorToJSONObject(const aos::oci::ContentDescriptor& descriptor);

/**
 * Converts platform from JSON object.
 *
 * @param object JSON object.
 * @param platform platform.
 */
void PlatformFromJSONObject(const utils::CaseInsensitiveObjectWrapper& object, aos::oci::Platform& platform);

/**
 * Converts platform to JSON object.
 *
 * @param platform platform.
 * @param object JSON object.
 */
void PlatformToJSONObject(const aos::oci::Platform& platform, Poco::JSON::Object& object);

} // namespace aos::common::oci

#endif
