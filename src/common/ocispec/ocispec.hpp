/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_OCISPEC_OCISPEC_HPP_
#define AOS_COMMON_OCISPEC_OCISPEC_HPP_

#include <core/common/ocispec/itf/ocispec.hpp>

namespace aos::common::oci {

/**
 * OCI spec instance.
 */
class OCISpec : public aos::oci::OCISpecItf {
public:
    /**
     * Loads OCI image index.
     *
     * @param path file path.
     * @param index image index.
     * @return Error.
     */
    Error LoadImageIndex(const String& path, aos::oci::ImageIndex& index) override;

    /**
     * Saves OCI image index.
     *
     * @param path file path.
     * @param index image index.
     * @return Error.
     */
    Error SaveImageIndex(const String& path, const aos::oci::ImageIndex& index) override;

    /**
     * Loads OCI image manifest.
     *
     * @param path file path.
     * @param manifest image manifest.
     * @return Error.
     */
    Error LoadImageManifest(const String& path, aos::oci::ImageManifest& manifest) override;

    /**
     * Saves OCI image manifest.
     *
     * @param path file path.
     * @param manifest image manifest.
     * @return Error.
     */
    Error SaveImageManifest(const String& path, const aos::oci::ImageManifest& manifest) override;

    /**
     * Loads OCI image config.
     *
     * @param path file path.
     * @param imageConfig image config.
     * @return Error.
     */
    Error LoadImageConfig(const String& path, aos::oci::ImageConfig& imageConfig) override;

    /**
     * Saves OCI image config.
     *
     * @param path file path.
     * @param imageConfig image config.
     * @return Error.
     */
    Error SaveImageConfig(const String& path, const aos::oci::ImageConfig& imageConfig) override;

    /**
     * Loads Aos service config.
     *
     * @param path file path.
     * @param serviceConfig service config.
     * @return Error.
     */
    Error LoadServiceConfig(const String& path, aos::oci::ServiceConfig& serviceConfig) override;

    /**
     * Saves Aos service config.
     *
     * @param path file path.
     * @param serviceConfig service config.
     * @return Error.
     */
    Error SaveServiceConfig(const String& path, const aos::oci::ServiceConfig& serviceConfig) override;

    /**
     * Loads Aos component config.
     *
     * @param path file path.
     * @param componentConfig component config.
     * @return Error.
     */
    Error LoadComponentConfig(const String& path, aos::oci::ComponentConfig& componentConfig) override;

    /**
     * Saves Aos component config.
     *
     * @param path file path.
     * @param componentConfig component config.
     * @return Error.
     */
    Error SaveComponentConfig(const String& path, const aos::oci::ComponentConfig& componentConfig) override;

    /**
     * Loads OCI runtime config.
     *
     * @param path file path.
     * @param runtimeConfig runtime config.
     * @return Error.
     */
    Error LoadRuntimeConfig(const String& path, aos::oci::RuntimeConfig& runtimeConfig) override;

    /**
     * Saves OCI runtime config.
     *
     * @param path file path.
     * @param runtimeConfig runtime config.
     * @return Error.
     */
    Error SaveRuntimeConfig(const String& path, const aos::oci::RuntimeConfig& runtimeConfig) override;
};

} // namespace aos::common::oci

#endif
