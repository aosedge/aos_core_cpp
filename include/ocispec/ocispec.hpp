/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OCISPEC_HPP_
#define OCISPEC_HPP_

#include <aos/common/ocispec/ocispec.hpp>

namespace aos::common::oci {

/**
 * OCI spec instance.
 */
class OCISpec : public aos::oci::OCISpecItf {
public:
    /**
     * Loads OCI image manifest.
     *
     * @param path file path.
     * @param[out] manifest image manifest.
     * @return Error.
     */
    Error LoadImageManifest(const String& path, aos::oci::ImageManifest& manifest) override
    {
        (void)path;
        (void)manifest;

        return ErrorEnum::eNone;
    }

    /**
     * Saves OCI image manifest.
     *
     * @param path file path.
     * @param manifest image manifest.
     * @return Error.
     */
    Error SaveImageManifest(const String& path, const aos::oci::ImageManifest& manifest) override
    {
        (void)path;
        (void)manifest;

        return ErrorEnum::eNone;
    }

    /**
     * Loads OCI image spec.
     *
     * @param path file path.
     * @param[out] imageSpec image spec.
     * @return Error.
     */
    // cppcheck-suppress constParameterReference
    Error LoadImageSpec(const String& path, aos::oci::ImageSpec& imageSpec)
    {
        (void)path;
        (void)imageSpec;

        return ErrorEnum::eNone;
    }

    /**
     * Saves OCI image spec.
     *
     * @param path file path.
     * @param imageSpec image spec.
     * @return Error.
     */
    Error SaveImageSpec(const String& path, const aos::oci::ImageSpec& imageSpec)
    {
        (void)path;
        (void)imageSpec;

        return ErrorEnum::eNone;
    }

    /**
     * Loads OCI runtime spec.
     *
     * @param path file path.
     * @param[out] runtimeSpec runtime spec.
     * @return Error.
     */
    // cppcheck-suppress constParameterReference
    Error LoadRuntimeSpec(const String& path, aos::oci::RuntimeSpec& runtimeSpec)
    {
        (void)path;
        (void)runtimeSpec;

        return ErrorEnum::eNone;
    }

    /**
     * Saves OCI runtime spec.
     *
     * @param path file path.
     * @param runtimeSpec runtime spec.
     * @return Error.
     */
    Error SaveRuntimeSpec(const String& path, const aos::oci::RuntimeSpec& runtimeSpec)
    {
        (void)path;
        (void)runtimeSpec;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::common::oci

#endif
