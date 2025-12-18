/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_ALERTS_ITF_STORAGE_HPP_
#define AOS_SM_ALERTS_ITF_STORAGE_HPP_

#include <core/common/types/common.hpp>

namespace aos::sm::alerts {

/** @addtogroup sm Service Manager
 *  @{
 */

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

/** @}*/

} // namespace aos::sm::alerts

#endif
