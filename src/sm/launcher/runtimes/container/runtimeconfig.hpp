/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_RUNTIMECONFIG_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_RUNTIMECONFIG_HPP_

#include <core/common/ocispec/itf/ocispec.hpp>

namespace aos::sm::launcher {

/**
 * Adds mount entry to runtime config.
 *
 * @param mount mount entry to add.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error AddMount(const Mount& mount, oci::RuntimeConfig& runtimeConfig);

/**
 * Adds namespace path.
 *
 * @param ns OCI Linux namespace.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error AddNamespace(const oci::LinuxNamespace& ns, oci::RuntimeConfig& runtimeConfig);

/**
 * Adds environment variables.
 *
 * @param envVars environment variables to set.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error AddEnvVars(const Array<StaticString<cEnvVarLen>>& envVars, oci::RuntimeConfig& runtimeConfig);

/**
 * Sets CPU limit.
 *
 * @param quota quota.
 * @param period period.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error SetCPULimit(int64_t quota, uint64_t period, oci::RuntimeConfig& runtimeConfig);

/**
 * Sets RAM limit.
 *
 * @param limit RAM limit.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error SetRAMLimit(int64_t limit, oci::RuntimeConfig& runtimeConfig);

/**
 * Sets PID limit.
 *
 * @param limit PID limit.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error SetPIDLimit(int64_t limit, oci::RuntimeConfig& runtimeConfig);

/**
 * Adds rlimit.
 *
 * @param rlimit rlimit to add.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error AddRLimit(const oci::POSIXRlimit& rlimit, oci::RuntimeConfig& runtimeConfig);

/**
 * Adds additional GID.
 *
 * @param gid GID to add.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error AddAdditionalGID(uint32_t gid, oci::RuntimeConfig& runtimeConfig);

/**
 * Adds device.
 *
 * @param device device to add.
 * @param permissions device permissions.
 * @param runtimeConfig runtime config.
 * @return Error.
 */
Error AddDevice(const oci::LinuxDevice& device, const StaticString<cPermissionsLen>& permissions,
    oci::RuntimeConfig& runtimeConfig);

} // namespace aos::sm::launcher

#endif
