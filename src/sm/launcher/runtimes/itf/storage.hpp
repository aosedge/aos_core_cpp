/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_ITF_STORAGE_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_ITF_STORAGE_HPP_

#include <core/common/types/instance.hpp>

namespace aos::sm::launcher::runtimes {

/** @addtogroup sm Service Manager
 *  @{
 */

/**
 * Runtime instance data.
 *
 * Instance is identified by runtime ID, instance ident and manifest digest: a runtime may keep several versions of the
 * same instance, e.g. the current and the pending one during update.
 */
struct RuntimeInstanceData : public InstanceIdent {
    StaticString<cIDLen>          mRuntimeID;
    StaticString<oci::cDigestLen> mManifestDigest;
    StaticString<cVersionLen>     mVersion;
    InstanceState                 mState;
    Error                         mError;

    /**
     * Compares runtime instance data.
     *
     * @param rhs runtime instance data to compare with.
     * @return bool.
     */
    bool operator==(const RuntimeInstanceData& rhs) const
    {
        return static_cast<const InstanceIdent&>(*this) == static_cast<const InstanceIdent&>(rhs)
            && mRuntimeID == rhs.mRuntimeID && mManifestDigest == rhs.mManifestDigest && mVersion == rhs.mVersion
            && mState == rhs.mState && mError == rhs.mError && String(mError.Message()) == rhs.mError.Message();
    }

    /**
     * Compares runtime instance data.
     *
     * @param rhs runtime instance data to compare with.
     * @return bool.
     */
    bool operator!=(const RuntimeInstanceData& rhs) const { return !operator==(rhs); }
};

/**
 * Runtime storage interface.
 */
class StorageItf {
public:
    /**
     * Destructor.
     */
    virtual ~StorageItf() = default;

    /**
     * Adds runtime instance to storage.
     *
     * @param instance runtime instance data.
     * @return Error.
     */
    virtual Error AddRuntimeInstance(const RuntimeInstanceData& instance) = 0;

    /**
     * Updates runtime instance in storage.
     *
     * @param instance runtime instance data.
     * @return Error.
     */
    virtual Error UpdateRuntimeInstance(const RuntimeInstanceData& instance) = 0;

    /**
     * Removes runtime instance from storage.
     *
     * @param runtimeID runtime ID.
     * @param ident instance ident.
     * @param manifestDigest instance manifest digest.
     * @return Error.
     */
    virtual Error RemoveRuntimeInstance(
        const String& runtimeID, const InstanceIdent& ident, const String& manifestDigest)
        = 0;

    /**
     * Returns all instances of the runtime.
     *
     * @param runtimeID runtime ID.
     * @param[out] instances runtime instances.
     * @return Error.
     */
    virtual Error GetRuntimeInstances(const String& runtimeID, Array<RuntimeInstanceData>& instances) const = 0;

    /**
     * Begins a storage transaction; subsequent writes are staged until commit.
     *
     * @return Error.
     */
    virtual Error BeginTransaction() = 0;

    /**
     * Commits the current storage transaction.
     *
     * @return Error.
     */
    virtual Error CommitTransaction() = 0;

    /**
     * Rolls back the current storage transaction, discarding its writes.
     *
     * @return Error.
     */
    virtual Error RollbackTransaction() = 0;
};

/** @}*/

} // namespace aos::sm::launcher::runtimes

#endif
