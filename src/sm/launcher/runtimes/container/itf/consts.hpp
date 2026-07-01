/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_CONSTS_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_CONSTS_HPP_

namespace aos::sm::launcher {

/**
 * Cgroup path (relative to the cgroupfs mount root) instances are placed under. Used both as the OCI runtime
 * config's cgroupsPath and by Monitoring to locate per-instance cgroup stat files.
 */
constexpr auto cCgroupsPath = "/system.slice/system-aos.slice/system-aos-service.slice";

} // namespace aos::sm::launcher

#endif
