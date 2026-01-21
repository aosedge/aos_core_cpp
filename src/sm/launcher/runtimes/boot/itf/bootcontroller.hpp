/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_ITF_BOOTCONTROLLER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_ITF_BOOTCONTROLLER_HPP_

#include <string>
#include <vector>

#include <core/common/types/common.hpp>

#include <sm/launcher/runtimes/boot/config.hpp>

namespace aos::sm::launcher {

/**
 * Boot controller interface.
 */
class BootControllerItf {
public:
    /**
     * Destructor.
     */
    virtual ~BootControllerItf() = default;

    /**
     * Initializes boot controller.
     *
     * @param config boot config.
     * @return Error.
     */
    virtual Error Init(const BootConfig& config) = 0;

    /**
     * Returns boot partition devices.
     *
     * @param[out] devices boot partition devices.
     * @return Error.
     */
    virtual Error GetPartitionDevices(std::vector<std::string>& devices) const = 0;

    /**
     * Returns current boot index.
     *
     * @return RetWithError<size_t>.
     */
    virtual RetWithError<size_t> GetCurrentBoot() const = 0;

    /**
     * Returns main boot index.
     *
     * @return RetWithError<size_t>.
     */
    virtual RetWithError<size_t> GetMainBoot() const = 0;

    /**
     * Sets the main boot partition index.
     *
     * @param index main boot partition index.
     * @return Error.
     */
    virtual Error SetMainBoot(size_t index) = 0;

    /**
     * Sets boot successful flag.
     *
     * @return Error.
     */
    virtual Error SetBootOK() = 0;
};

} // namespace aos::sm::launcher

#endif
