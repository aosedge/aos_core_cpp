/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_UNITCONFIG_JSONPROVIDER_HPP_
#define AOS_CM_UNITCONFIG_JSONPROVIDER_HPP_

#include <core/cm/unitconfig/itf/jsonprovider.hpp>

namespace aos::cm::unitconfig {

/**
 * JSON provider for UnitConfig.
 */
class JSONProvider : public unitconfig::JSONProviderItf {
public:
    /**
     * Creates unit config object from a JSON string.
     *
     * @param json JSON string.
     * @param[out] unitConfig unit config object.
     * @return Error.
     */
    Error UnitConfigFromJSON(const String& json, aos::UnitConfig& unitConfig) const override;

    /**
     * Creates unit config JSON string from a unit config object.
     *
     * @param unitConfig unit config object.
     * @param[out] json JSON string.
     * @return Error.
     */
    Error UnitConfigToJSON(const aos::UnitConfig& unitConfig, String& json) const override;
};

} // namespace aos::cm::unitconfig

#endif
