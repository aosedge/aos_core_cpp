/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_INSTANCEIDPROVIDER_HPP_
#define AOS_SM_LAUNCHER_INSTANCEIDPROVIDER_HPP_

#include <memory>
#include <vector>

#include <core/sm/launcher/itf/instanceidprovider.hpp>

namespace aos::sm::launcher {

/**
 * Instance ID provider.
 */
class InstanceIDProvider : public InstanceIDProviderItf {
public:
    /**
     * Returns instance ID.
     *
     * @param instance instance ident.
     * @param[out] instanceID instance ID.
     * @return Error.
     */
    Error GetInstanceID(const InstanceIdent& instance, String& instanceID) const override;
};

} // namespace aos::sm::launcher

#endif
