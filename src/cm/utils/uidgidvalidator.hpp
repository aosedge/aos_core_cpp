/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_UTILS_UIDGIDVALIDATOR_HPP_
#define AOS_CM_UTILS_UIDGIDVALIDATOR_HPP_

#include <cstddef>

namespace aos::cm::utils {

/**
 * Checks if UID is valid.
 *
 * @param uid user identifier.
 * @return bool.
 */
bool IsUIDValid(size_t uid);

/**
 * Checks if GID is valid.
 *
 * @param gid group identifier.
 * @return bool.
 */
bool IsGIDValid(size_t gid);

} // namespace aos::cm::utils

#endif
