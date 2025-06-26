/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_UIDGIDPOOL_UIDGIDPOOL_HPP_
#define AOS_CM_UIDGIDPOOL_UIDGIDPOOL_HPP_

#include <functional>
#include <memory>
#include <set>

#include <common/logger/logger.hpp>

namespace aos::cm::uidgidpool {

/**
 * Interface for identifier pool.
 */
class IdentifierPoolItf : public NonCopyable {
public:
    /**
     * Returns free identifier from pool.
     *
     * @return RetWithError<size_t>.
     */
    virtual RetWithError<size_t> GetFreeID() = 0;

    /**
     * Locks identifier in pool.
     *
     * @param id identifier to lock.
     * @return Error.
     */
    virtual Error LockID(size_t id) = 0;

    /**
     * Releases identifier in pool.
     *
     * @param id identifier to release.
     * @return Error.
     */
    virtual Error ReleaseID(size_t id) = 0;

    /**
     * Destructor.
     */
    virtual ~IdentifierPoolItf() = default;
};

/**
 * Identifier pool.
 */
class IdentifierPool : public IdentifierPoolItf {
public:
    /**
     * Returns free identifier from pool.
     *
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetFreeID() override;

    /**
     * Locks identifier in pool.
     *
     * @param id identifier to lock.
     * @return Error.
     */
    Error LockID(size_t id) override;

    /**
     * Releases identifier in pool.
     *
     * @param id identifier to release.
     * @return Error.
     */
    Error ReleaseID(size_t id) override;

private:
    static constexpr auto cIdsRangeBegin = 5000;
    static constexpr auto cIdsRangeEnd   = 10000;

    virtual bool IdIsValid(size_t id) = 0;

    std::mutex       mMutex;
    std::set<size_t> mLockedIds;
};

/**
 * UID pool.
 */
class UIDPool : public IdentifierPool {
private:
    bool IdIsValid(size_t id) override;
};

/**
 * GID pool.
 */
class GIDPool : public IdentifierPool {
private:
    bool IdIsValid(size_t id) override;
};

} // namespace aos::cm::uidgidpool

#endif
