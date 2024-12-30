/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include "utils/filesystem.hpp"

namespace fs = std::filesystem;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RetWithError<std::string> MkTmpDir(const std::string& dir, const std::string& pattern)
{
    std::string directory   = dir.empty() ? fs::temp_directory_path().string() : dir;
    std::string tempPattern = pattern.empty() ? "tmp.XXXXXX" : pattern;

    if (tempPattern.length() < 7 || tempPattern.substr(tempPattern.length() - 7) != ".XXXXXX") {
        tempPattern += ".XXXXXX";
    }

    std::string fullPath = (fs::path(directory) / tempPattern).string();

    std::vector<char> mutablePath(fullPath.begin(), fullPath.end());
    mutablePath.push_back('\0');

    char* result = mkdtemp(mutablePath.data());

    if (result == nullptr) {
        return {"", Error(ErrorEnum::eFailed, strerror(errno))};
    }

    return {std::string(result), ErrorEnum::eNone};
}

} // namespace aos::common::utils
