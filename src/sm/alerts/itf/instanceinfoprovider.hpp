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
 * Service instance data.
 */
struct ServiceInstanceData {
    InstanceIdent             mInstanceIdent;
    StaticString<cVersionLen> mVersion;

    /**
     * Equality operator.
     */
    bool operator==(const ServiceInstanceData& other) const
    {
        return mInstanceIdent == other.mInstanceIdent && mVersion == other.mVersion;
    }

    /**
     * Equality operator.
     */
    bool operator!=(const ServiceInstanceData& other) const { return !(*this == other); }
};

/**
 * Provides service instances info.
 */
class InstanceInfoProviderItf {
public:
    /**
     * Returns service instance info.
     *
     * @param id instance id.
     * @param[out] instanceData service instance data.
     * @return Error.
     */
    virtual Error GetInstanceInfoByID(const String& id, ServiceInstanceData& instanceData) = 0;

    /**
     * Destructor.
     */
    virtual ~InstanceInfoProviderItf() = default;
};

/** @}*/

} // namespace aos::sm::alerts

#endif
