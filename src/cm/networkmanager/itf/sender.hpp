/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_ITF_SENDER_HPP_
#define AOS_CM_NETWORKMANAGER_ITF_SENDER_HPP_

#include <vector>

#include <core/common/types/network.hpp>

namespace aos::cm::networkmanager {

/**
 * Sender interface.
 */
class SenderItf {
public:
    /**
     * Destructor.
     */
    virtual ~SenderItf() = default;

    /**
     * Sends network.
     *
     * @param nodeID Node ID.
     * @param networkParameters Network parameters.
     * @return Error.
     */
    virtual Error SendNetwork(const std::string& nodeID, const std::vector<NetworkParameters>& networkParameters) = 0;
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_ITF_SENDER_HPP_
