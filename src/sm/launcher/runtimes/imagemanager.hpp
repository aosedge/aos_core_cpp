/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_IMAGEMANAGER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_IMAGEMANAGER_HPP_

#include <core/common/tools/string.hpp>

namespace aos::sm::launcher {

/**
 * Image manager interface.
 */
class ImageManagerItf {
public:
    /**
     * Destructor.
     */
    virtual ~ImageManagerItf() = default;

    /**
     * Returns blob path by its digest.
     *
     * @param digest blob digest.
     * @param[out] path result blob path.
     * @return Error.
     */
    virtual Error GetBlobPath(const String& digest, String& path) const = 0;
};

} // namespace aos::sm::launcher

#endif
