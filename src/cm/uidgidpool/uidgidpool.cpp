/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>

#include <common/logger/logmodule.hpp>

#include "uidgidpool.hpp"

namespace aos::cm::uidgidpool {

/***********************************************************************************************************************
 * IdentifierPool
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RetWithError<size_t> IdentifierPool::GetFreeID()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Getting free identifier from pool";

    for (size_t id = cIdsRangeBegin; id < cIdsRangeEnd; id++) {
        if (mLockedIds.find(id) != mLockedIds.end()) {
            continue;
        }

        if (!IdIsValid(id)) {
            continue;
        }

        mLockedIds.insert(id);

        return RetWithError<size_t>(id);
    }

    return {{}, ErrorEnum::eNotFound};
}

Error IdentifierPool::LockID(size_t id)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Lock identifier" << Log::Field("id", id);

    if (id < cIdsRangeBegin || id >= cIdsRangeEnd) {
        return ErrorEnum::eOutOfRange;
    }

    if (mLockedIds.find(id) != mLockedIds.end()) {
        return ErrorEnum::eFailed;
    }

    mLockedIds.insert(id);

    return ErrorEnum::eNone;
}

Error IdentifierPool::ReleaseID(size_t id)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Release identifier" << Log::Field("id", id);

    auto it = mLockedIds.find(id);
    if (it == mLockedIds.end()) {
        return ErrorEnum::eNotFound;
    }

    mLockedIds.erase(it);

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * UIDPool
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

bool UIDPool::IdIsValid(size_t id)
{
    auto passwd = getpwuid(static_cast<uid_t>(id));

    return passwd == nullptr;
}

/***********************************************************************************************************************
 * GIDPool
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

bool GIDPool::IdIsValid(size_t id)
{
    auto group = getgrgid(static_cast<gid_t>(id));

    return group == nullptr;
}

} // namespace aos::cm::uidgidpool
