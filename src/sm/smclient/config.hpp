/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_SMCLIENT_CONFIG_HPP_
#define AOS_SM_SMCLIENT_CONFIG_HPP_

#include <string>

#include <common/utils/time.hpp>

namespace aos::sm::smclient {

/***
 * Service manager client configuration.
 */
struct Config {
    std::string mCertStorage;
    std::string mCMServerURL;
    Duration    mCMReconnectTimeout;
};

} // namespace aos::sm::smclient

#endif
