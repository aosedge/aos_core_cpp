/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SERVICEIMAGE_HPP_
#define SERVICEIMAGE_HPP_

#include <string>

#include <aos/common/tools/error.hpp>

namespace aos::mp::imageunpacker {

/**
 * Unpacks service.
 *
 * @param archivePath archive path.
 * @param imageStoreDir image store directory.
 * @return RetWithError<std::string>.
 */
RetWithError<std::string> UnpackService(const std::string& archivePath, const std::string& imageStoreDir);

} // namespace aos::mp::imageunpacker

#endif
