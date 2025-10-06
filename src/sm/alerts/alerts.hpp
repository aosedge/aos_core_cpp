/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_ALERTS_ALERTS_HPP_
#define AOS_SM_ALERTS_ALERTS_HPP_

#include <core/common/alerts/alerts.hpp>
#include <core/common/types/alerts.hpp>

namespace aos::sm::alerts {

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

/*
 * Provides service instances info.
 */
class InstanceInfoProviderItf {
public:
    /**
     * Returns service instance info.
     *
     * @param id instance id.
     * @return RetWithError<ServiceInstanceData>.
     */
    virtual RetWithError<ServiceInstanceData> GetInstanceInfoByID(const String& id) = 0;

    /**
     * Destructor.
     */
    virtual ~InstanceInfoProviderItf() = default;
};

/**
 * Storage interface.
 */
class StorageItf {
public:
    /**
     * Sets journal cursor.
     *
     * @param cursor journal cursor.
     * @return Error.
     */
    virtual Error SetJournalCursor(const String& cursor) = 0;

    /**
     * Gets journal cursor.
     *
     * @param cursor[out] journal cursor.
     * @return Error.
     */
    virtual Error GetJournalCursor(String& cursor) const = 0;

    /**
     * Destructor.
     */
    virtual ~StorageItf() = default;
};

} // namespace aos::sm::alerts

#endif
