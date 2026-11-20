/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_ITF_UPDATESUBSCRIPTIONLISTENER_HPP_
#define AOS_CM_NETWORKMANAGER_ITF_UPDATESUBSCRIPTIONLISTENER_HPP_

#include <core/common/tools/string.hpp>

namespace aos::cm::networkmanager {

/**
 * Network update subscription listener interface.
 */
class UpdateSubscriptionListenerItf {
public:
    /**
     * Destructor.
     */
    virtual ~UpdateSubscriptionListenerItf() = default;

    /**
     * Called when node subscribes to network updates.
     *
     * @param nodeID Node ID.
     */
    virtual void OnNodeSubscribed(const String& nodeID) = 0;

    /**
     * Called when node unsubscribes from network updates.
     *
     * @param nodeID Node ID.
     */
    virtual void OnNodeUnsubscribed(const String& nodeID) = 0;
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_ITF_UPDATESUBSCRIPTIONLISTENER_HPP_
