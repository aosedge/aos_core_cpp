/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JSONPROVIDER_HPP
#define JSONPROVIDER_HPP

#include <aos/sm/resourcemanager.hpp>

namespace aos::common::jsonprovider {

/**
 * JSON provider.
 */
class JSONProvider : public sm::resourcemanager::JSONProviderItf {
public:
    /**
     * Dumps config object into string.
     *
     * @param nodeConfig node config object.
     * @param[out] json node config JSON string.
     * @return Error.
     */
    Error NodeConfigToJSON(const sm::resourcemanager::NodeConfig& nodeConfig, String& json) const override;

    /**
     * Creates node config object from a JSON string.
     *
     * @param json node config JSON string.
     * @param[out] nodeConfig node config object.
     * @return Error.
     */
    Error NodeConfigFromJSON(const String& json, sm::resourcemanager::NodeConfig& nodeConfig) const override;
};

} // namespace aos::common::jsonprovider

#endif
