/*
 * Copyright (C) 2026 EPAM Systems, Inc.
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
 * Instance is identified by runtime ID, instance ident (except the preinstalled flag) and version: a runtime may keep
 * several versions of the same instance, e.g. the current and the pending one during update.
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
     * @param lhs runtime instance data to compare.
     * @param rhs runtime instance data to compare with.
     * @return bool.
     */
    friend bool operator==(const RuntimeInstanceData& lhs, const RuntimeInstanceData& rhs)
    {
        return static_cast<const InstanceIdent&>(lhs) == static_cast<const InstanceIdent&>(rhs)
            && lhs.mRuntimeID == rhs.mRuntimeID && lhs.mManifestDigest == rhs.mManifestDigest
            && lhs.mVersion == rhs.mVersion && lhs.mState == rhs.mState && lhs.mError == rhs.mError
            && String(lhs.mError.Message()) == rhs.mError.Message();
    }

    /**
     * Compares runtime instance data.
     *
     * @param lhs runtime instance data to compare.
     * @param rhs runtime instance data to compare with.
     * @return bool.
     */
    friend bool operator!=(const RuntimeInstanceData& lhs, const RuntimeInstanceData& rhs) { return !(lhs == rhs); }
};

/**
 * Runtime storage interface.
 *
 * Each call is atomic and durable on return: runtimes rely on it to recover after power loss.
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
     * @param version instance version.
     * @return Error.
     */
    virtual Error RemoveRuntimeInstance(const String& runtimeID, const InstanceIdent& ident, const String& version) = 0;

    /**
     * Returns all instances of the runtime.
     *
     * @param runtimeID runtime ID.
     * @param[out] instances runtime instances.
     * @return Error.
     */
    virtual Error GetRuntimeInstances(const String& runtimeID, Array<RuntimeInstanceData>& instances) const = 0;
};

/** @}*/

} // namespace aos::sm::launcher::runtimes

#endif
