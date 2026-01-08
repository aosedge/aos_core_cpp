/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONFIG_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONFIG_HPP_

#include <Poco/JSON/Object.h>
#include <string>

namespace aos::sm::launcher {

/**
 * Runtime configuration.
 */
struct RuntimeConfig {
    std::string             mPlugin;
    std::string             mType;
    bool                    isComponent {};
    std::string             mWorkingDir;
    Poco::JSON::Object::Ptr mConfig;
};

} // namespace aos::sm::launcher

#endif
