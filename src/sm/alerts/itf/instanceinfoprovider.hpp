/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_ALERTS_ITF_INSTANCEINFOPROVIDER_HPP_
#define AOS_SM_ALERTS_ITF_INSTANCEINFOPROVIDER_HPP_

#include <core/common/types/common.hpp>

namespace aos::sm::alerts {

/** @addtogroup sm Service Manager
 *  @{
 */

/**
 * Instance info.
 */
struct InstanceInfo {
    InstanceIdent             mInstanceIdent;
    StaticString<cVersionLen> mVersion;

    /**
     * Equality operator.
     */
    bool operator==(const InstanceInfo& rhs) const
    {
        return mInstanceIdent == rhs.mInstanceIdent && mVersion == rhs.mVersion;
    }

    /**
     * Inequality operator.
     */
    bool operator!=(const InstanceInfo& rhs) const { return !(*this == rhs); }
};

/**
 * Provides service instances info.
 */
class InstanceInfoProviderItf {
public:
    /**
     * Destructor.
     */
    virtual ~InstanceInfoProviderItf() = default;

    /**
     * Returns service instance info.
     *
     * @param instanceID instance ID.
     * @param[out] instanceInfo instance info.
     * @return Error.
     */
    virtual Error GetInstanceInfoByID(const String& instanceID, InstanceInfo& instanceInfo) = 0;
};

/** @}*/

} // namespace aos::sm::alerts

#endif
