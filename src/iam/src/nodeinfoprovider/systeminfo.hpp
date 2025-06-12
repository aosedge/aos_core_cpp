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

namespace aos::iam::nodeinfoprovider::utils {

/**
 * Gets CPU information from the specified file.
 *
 * @param path Path to the file with CPU information.
 * @param[out] cpuInfoArray Array to store CPU information.
 * @return Error.
 */
Error GetCPUInfo(const std::string& path, Array<CPUInfo>& cpuInfoArray) noexcept;

/**
 * Gets the total memory size.
 *
 * @param path Path to the memory information file.
 * @return RetWithError<uint64_t>.
 */
RetWithError<uint64_t> GetMemTotal(const std::string& path) noexcept;

/**
 * Gets the total size of the specified mount point.
 *
 * @param path Path to the mount point.
 * @return RetWithError<uint64_t>.
 */
RetWithError<uint64_t> GetMountFSTotalSize(const std::string& path) noexcept;

} // namespace aos::iam::nodeinfoprovider::utils

#endif
