/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CNI_HPP_
#define CNI_HPP_

#include <aos/sm/cni.hpp>

namespace aos::sm::cni {

/**
 * CNI instance.
 */
class CNI : public CNIItf {
public:
    /**
     * Initializes CNI.
     *
     * @param cniConfigDir Path to CNI configuration directory.
     * @return Error.
     */
    Error Init(const String& configDir) override
    {
        (void)configDir;

        return ErrorEnum::eNone;
    }

    /**
     * Executes a sequence of plugins with the ADD command
     *
     * @param net List of network configurations.
     * @param rt Runtime configuration parameters.
     * @return RetWithError<Result>.
     */
    RetWithError<Result> AddNetworkList(const NetworkConfigList& net, const RuntimeConf& rt) override
    {
        (void)net;
        (void)rt;

        return {Result(), ErrorEnum::eNone};
    }

    /**
     * Executes a sequence of plugins with the DEL command
     *
     * @param net List of network configurations.
     * @param rt Runtime configuration parameters.
     * @return Error.
     */
    Error DeleteNetworkList(const NetworkConfigList& net, const RuntimeConf& rt) override
    {
        (void)net;
        (void)rt;

        return ErrorEnum::eNone;
    }

    /**
     * Checks that a configuration is reasonably valid.
     *
     * @param net List of network configurations.
     * @return Error.
     */
    Error ValidateNetworkList(const NetworkConfigList& net) override
    {
        (void)net;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::sm::cni

#endif // CNI_HPP_
