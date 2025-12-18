/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LOGPROVIDER_ITF_INSTANCEIDPROVIDER_HPP_
#define AOS_SM_LOGPROVIDER_ITF_INSTANCEIDPROVIDER_HPP_

#include <string>
#include <vector>

#include <core/common/types/log.hpp>

namespace aos::sm::logprovider {

/** @addtogroup sm Service Manager
 *  @{
 */

/**
 * Provides service instances ID.
 */
class InstanceIDProviderItf {
public:
    /**
     * Returns service instance IDs.
     *
     * @param filter log filter.
     * @param[out] instanceIDs instance IDs.
     * @return Error.
     */
    virtual Error GetInstanceIDs(const LogFilter& filter, std::vector<std::string>& instanceIDs) = 0;

    /**
     * Destructor.
     */
    virtual ~InstanceIDProviderItf() = default;
};

/** @}*/

} // namespace aos::sm::logprovider

#endif
