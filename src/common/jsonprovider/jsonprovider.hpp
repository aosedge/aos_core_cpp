/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_JSONPROVIDER_JSONPROVIDER_HPP_
#define AOS_COMMON_JSONPROVIDER_JSONPROVIDER_HPP_

#include <core/common/nodeconfig/itf/jsonprovider.hpp>

namespace aos::common::jsonprovider {

/**
 * JSON provider.
 */
class JSONProvider : public nodeconfig::JSONProviderItf {
public:
    /**
     * Dumps config object into string.
     *
     * @param nodeConfig node config object.
     * @param[out] json node config JSON string.
     * @return Error.
     */
    Error NodeConfigToJSON(const NodeConfig& nodeConfig, String& json) const override;

    /**
     * Creates node config object from a JSON string.
     *
     * @param json node config JSON string.
     * @param[out] nodeConfig node config object.
     * @return Error.
     */
    Error NodeConfigFromJSON(const String& json, NodeConfig& nodeConfig) const override;
};

} // namespace aos::common::jsonprovider

#endif
