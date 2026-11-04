/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_HTTPSTATUS_HPP_
#define AOS_COMMON_UTILS_HTTPSTATUS_HPP_

namespace aos::common::utils {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

/**
 * HTTP status reported by the file server when a stored file fails the integrity check.
 */
constexpr int cIntegrityHTTPStatus = 512;

/**
 * HTTP reason phrase for the integrity status.
 */
constexpr auto cIntegrityHTTPReason = "Integrity error";

} // namespace aos::common::utils

#endif // AOS_COMMON_UTILS_HTTPSTATUS_HPP_
