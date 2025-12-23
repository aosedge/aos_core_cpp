/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_HPP_

#include <memory>
#include <vector>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/sm/launcher/itf/runtime.hpp>

#include "config.hpp"

namespace aos::sm::launcher {

/**
 * Runtimes factory.
 */
class Runtimes {
public:
    /**
     * Initializes runtimes.
     *
     * @param config runtime config.
     * @param currentNodeInfoProvider current node info provider.
     * @return Error.
     */
    Error Init(const Config& config, aos::iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider);

    /**
     * Returns runtimes.
     *
     * @param[out] runtimes array to store runtimes.
     * @return Error.
     */
    Error GetRuntimes(Array<RuntimeItf*>& runtimes) const;

private:
    std::vector<std::unique_ptr<RuntimeItf>> mRuntimes;
};

} // namespace aos::sm::launcher

#endif
