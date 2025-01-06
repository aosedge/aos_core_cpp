/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "logger/logmodule.hpp"

#include "runtime.hpp"

namespace aos::sm::launcher {
namespace fs = std::filesystem;

namespace {

/***********************************************************************************************************************
 * consts
 **********************************************************************************************************************/

constexpr auto cDirPermissions = fs::perms::owner_all | fs::perms::group_exec | fs::perms::group_read
    | fs::perms::others_exec | fs::perms::others_read;

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Runtime::CreateHostFSWhiteouts(const String& path, const Array<StaticString<cFilePathLen>>& hostBinds)
{
    try {
        auto destPath = fs::path(path.CStr());

        fs::create_directories(destPath);
        fs::permissions(destPath, cDirPermissions);

        for (const auto& entry : fs::directory_iterator("/")) {
            if (hostBinds
                    .FindIf([&entry](const StaticString<cFilePathLen>& bind) {
                        return entry.path() == fs::path("/") / bind.CStr();
                    })
                    .mError.IsNone()) {
                continue;
            }

            auto itemPath(fs::path(destPath) += entry.path());

            if (fs::exists(itemPath)) {
                continue;
            }

            LOG_DBG() << "Create rootfs white out: path=" << itemPath.c_str();

            if (auto ret = mknod(itemPath.c_str(), S_IFCHR, makedev(0, 0)); ret != 0) {
                return Error(ErrorEnum::eRuntime, "can't create white out");
            }
        }
    }

    catch (const std::exception& e) {
        return Error(ErrorEnum::eRuntime, e.what());
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
