/*
 * Copyright (C) 2025 EPAM Systems, Inc.
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
     * Equality operator.
     */
    bool operator!=(const InstanceInfo& rhs) const { return !(*this == rhs); }
};

/**
 * Provides service instances info.
 */
class InstanceInfoProviderItf {
public:
    /**
     * Returns service instance info.
     *
     * @param instanceID instance id.
     * @param[out] instanceInfo instance info.
     * @return Error.
     */
    virtual Error GetInstanceInfoByID(const String& instanceID, InstanceInfo& instanceInfo) = 0;

    /**
     * Destructor.
     */
    virtual ~InstanceInfoProviderItf() = default;
};

/** @}*/

} // namespace aos::sm::alerts

#endif
