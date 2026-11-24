/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cerrno>
#include <grp.h>
#include <pwd.h>
#include <vector>

#include "uidgidvalidator.hpp"

namespace aos::cm::utils {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

constexpr size_t cEntryBufferSize = 1024;

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

bool IsUIDValid(size_t uid)
{
    struct passwd     entry  = {};
    struct passwd*    result = nullptr;
    std::vector<char> buffer(cEntryBufferSize);

    while (getpwuid_r(static_cast<uid_t>(uid), &entry, buffer.data(), buffer.size(), &result) == ERANGE) {
        buffer.resize(buffer.size() * 2);
    }

    return result == nullptr;
}

bool IsGIDValid(size_t gid)
{
    struct group      entry  = {};
    struct group*     result = nullptr;
    std::vector<char> buffer(cEntryBufferSize);

    while (getgrgid_r(static_cast<gid_t>(gid), &entry, buffer.data(), buffer.size(), &result) == ERANGE) {
        buffer.resize(buffer.size() * 2);
    }

    return result == nullptr;
}

} // namespace aos::cm::utils
