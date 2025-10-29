/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_CONFIG_HPP_
#define AOS_CM_SMCONTROLLER_CONFIG_HPP_

#include <string>

namespace aos::cm::smcontroller {

/**
 * SM controller configuration.
 */
struct Config {
    std::string mCMServerURL;
    std::string mCertStorage;
    std::string mCACert;
};

} // namespace aos::cm::smcontroller

#endif
