/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grp.h>
#include <pwd.h>

#include "uidgidvalidator.hpp"

namespace aos::cm::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

bool IsUIDValid(size_t uid)
{
    auto passwd = getpwuid(uid);
    if (passwd != nullptr) {
        return false;
    }

    return true;
}

bool IsGIDValid(size_t gid)
{
    auto group = getgrgid(gid);
    if (group != nullptr) {
        return false;
    }

    return true;
}

} // namespace aos::cm::utils
