/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_PARTITIONMANAGER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_PARTITIONMANAGER_HPP_

#include "itf/partitionmanager.hpp"

namespace aos::sm::launcher {

/**
 * Partition manager.
 */
class PartitionManager : public PartitionManagerItf {
public:
    /**
     * Returns partition info.
     *
     * @param partDevice partition device.
     * @param[out] partInfo partition info.
     * @return Error.
     */
    Error GetPartInfo(const std::string& partDevice, PartInfo& partInfo) const override;

    /**
     * Mounts partition.
     *
     * @param partInfo partition info.
     * @param mountPoint mount point.
     * @param flags mount flags.
     * @return Error.
     */
    Error Mount(const PartInfo& partInfo, const std::string& mountPoint, int flags) const override;

    /**
     * Unmounts partition.
     *
     * @param mountPoint mount point.
     * @return Error.
     */
    Error Unmount(const std::string& mountPoint) const override;

    /**
     * Copies device.
     *
     * @param src source device.
     * @param dst destination device.
     * @return Error.
     */
    Error CopyDevice(const std::string& src, const std::string& dst) const override;

    /**
     * Copies image to device.
     *
     * @param image image path.
     * @param device destination device.
     * @return Error.
     */
    Error InstallImage(const std::string& image, const std::string& device) const override;
};

} // namespace aos::sm::launcher

#endif
