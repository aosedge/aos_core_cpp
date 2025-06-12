/*
 * Copyright (C) 2025 EPAM Systems, Inc.
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
     * Loads OCI content descriptor.
     *
     * @param path file path.
     * @param descriptor[out]  content descriptor.
     * @return Error.
     */
    Error LoadContentDescriptor(const String& path, aos::oci::ContentDescriptor& descriptor) override;

    /**
     * Saves OCI content descriptor.
     *
     * @param path file path.
     * @param descriptor[out] content descriptor.
     * @return Error.
     */
    Error SaveContentDescriptor(const String& path, const aos::oci::ContentDescriptor& descriptor) override;

    /**
     * Loads OCI image manifest.
     *
     * @param path file path.
     * @param[out] manifest image manifest.
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
     * Loads OCI image spec.
     *
     * @param path file path.
     * @param[out] imageSpec image spec.
     * @return Error.
     */
    Error LoadImageSpec(const String& path, aos::oci::ImageSpec& imageSpec) override;

    /**
     * Saves OCI image spec.
     *
     * @param path file path.
     * @param imageSpec image spec.
     * @return Error.
     */
    Error SaveImageSpec(const String& path, const aos::oci::ImageSpec& imageSpec) override;

    /**
     * Loads OCI runtime spec.
     *
     * @param path file path.
     * @param[out] runtimeSpec runtime spec.
     * @return Error.
     */
    Error LoadRuntimeSpec(const String& path, aos::oci::RuntimeSpec& runtimeSpec) override;

    /**
     * Saves OCI runtime spec.
     *
     * @param path file path.
     * @param runtimeSpec runtime spec.
     * @return Error.
     */
    Error SaveRuntimeSpec(const String& path, const aos::oci::RuntimeSpec& runtimeSpec) override;

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
};

} // namespace aos::common::oci

#endif
