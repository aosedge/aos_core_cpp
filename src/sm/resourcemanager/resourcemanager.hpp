/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_RESOURCEMANAGER_RESOURCEMANAGER_HPP_
#define AOS_SM_RESOURCEMANAGER_RESOURCEMANAGER_HPP_

#include <vector>

#include <core/sm/resourcemanager/config.hpp>
#include <core/sm/resourcemanager/itf/resourceinfoprovider.hpp>

namespace aos::sm::resourcemanager {

/**
 * Resource manager.
 */
class ResourceManager : public ResourceInfoProviderItf {
public:
    /**
     * Initializes resource info provider.
     *
     * @param config resource manager configuration.
     * @return Error.
     */
    Error Init(const Config& config);

    /**
     * Returns resources info.
     *
     * @param[out] resources resources info.
     * @return Error.
     */
    Error GetResourcesInfos(Array<aos::ResourceInfo>& resources) override;

    /**
     * Returns resource info by name.
     *
     * @param name resource name.
     * @param[out] resourceInfo resource info.
     * @return Error.
     */
    Error GetResourceInfo(const String& name, ResourceInfo& resourceInfo) override;

private:
    Error ParseResourceInfos();

    Config                    mConfig;
    std::vector<ResourceInfo> mResources;
};

} // namespace aos::sm::resourcemanager

#endif
