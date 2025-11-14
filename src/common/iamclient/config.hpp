/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_CONFIG_HPP_
#define AOS_COMMON_IAMCLIENT_CONFIG_HPP_

#include <string>

namespace aos::common::iamclient {

/**
 * Configuration.
 */
struct Config {
    std::string mIAMPublicServerURL;
    std::string mCACert;
};

} // namespace aos::common::iamclient

#endif
