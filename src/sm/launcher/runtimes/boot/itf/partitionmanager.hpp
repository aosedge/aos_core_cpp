/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_ITF_PARTITIONMANAGER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_ITF_PARTITIONMANAGER_HPP_

#include <string>
#include <vector>

#include <sys/mount.h>

#include <core/common/types/common.hpp>

namespace aos::sm::launcher {

/**
 * Partition info.
 */
struct PartInfo {
    std::string mDevice;
    std::string mLabel;
    std::string mFSType;
    std::string mPartUUID;
    std::string mParentDevice;
    size_t      mPartitionNumber {};

    /**
     * Compares partition info.
     *
     * @param other other partition info.
     * @return bool.
     */
    bool operator==(const PartInfo& other) const
    {
        return mDevice == other.mDevice && mLabel == other.mLabel && mFSType == other.mFSType
            && mPartUUID == other.mPartUUID && mParentDevice == other.mParentDevice
            && mPartitionNumber == other.mPartitionNumber;
    }

    /**
     * Compares partition info.
     *
     * @param other other partition info.
     * @return bool.
     */
    bool operator!=(const PartInfo& other) const { return !(*this == other); }
};

/**
 * Partition manager interface.
 */
class PartitionManagerItf {
public:
    /**
     * Destructor.
     */
    virtual ~PartitionManagerItf() = default;

    /**
     * Returns partition info.
     *
     * @param partDevice partition device.
     * @param[out] partInfo partition info.
     * @return Error.
     */
    virtual Error GetPartInfo(const std::string& partDevice, PartInfo& partInfo) const = 0;

    /**
     * Mounts partition.
     *
     * @param partInfo partition info.
     * @param mountPoint mount point.
     * @param flags mount flags.
     * @return Error.
     */
    virtual Error Mount(const PartInfo& partInfo, const std::string& mountPoint, int flags) const = 0;

    /**
     * Unmounts partition.
     *
     * @param mountPoint mount point.
     * @return Error.
     */
    virtual Error Unmount(const std::string& mountPoint) const = 0;

    /**
     * Copies device.
     *
     * @param src source device.
     * @param dst destination device.
     * @return Error.
     */
    virtual Error CopyDevice(const std::string& src, const std::string& dst) const = 0;

    /**
     * Copies image to device.
     *
     * @param image image path.
     * @param device destination device.
     * @return Error.
     */
    virtual Error InstallImage(const std::string& image, const std::string& device) const = 0;
};

} // namespace aos::sm::launcher

#endif
