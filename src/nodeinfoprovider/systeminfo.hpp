/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYSTEMINFO_HPP_
#define SYSTEMINFO_HPP_

#include <string>

#include <aos/common/types.hpp>

namespace UtilsSystemInfo {

/**
 * Gets CPU information from the specified file.
 *
 * @param path Path to the file with CPU information.
 * @param[out] cpuInfoArray Array to store CPU information.
 * @return aos::Error.
 */
aos::Error GetCPUInfo(const std::string& path, aos::Array<aos::CPUInfo>& cpuInfoArray) noexcept;

/**
 * Gets the total memory size.
 *
 * @param path Path to the memory information file.
 * @return aos::RetWithError<uint64_t>.
 */
aos::RetWithError<uint64_t> GetMemTotal(const std::string& path) noexcept;

/**
 * Gets the total size of the specified mount point.
 *
 * @param path Path to the mount point.
 * @return aos::RetWithError<uint64_t>.
 */
aos::RetWithError<uint64_t> GetMountFSTotalSize(const std::string& path) noexcept;

} // namespace UtilsSystemInfo

#endif
