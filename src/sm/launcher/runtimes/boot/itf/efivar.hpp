/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_ITF_EFIVAR_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_ITF_EFIVAR_HPP_

#include <string>
#include <vector>

#include <core/common/tools/error.hpp>

namespace aos::sm::launcher {

/**
 * EFI variable interface.
 */
class EFIVarItf {
public:
    /**
     * Destructor.
     */
    virtual ~EFIVarItf() = default;

    /**
     * Reads EFI variable.
     *
     * @param name variable name.
     * @param data[out] variable data.
     * @param attributes[out] variable attributes.
     * @return Error.
     */
    virtual Error ReadVariable(const std::string& name, std::vector<uint8_t>& data, uint32_t& attributes) const = 0;

    /**
     * Writes EFI global GUID variable.
     *
     * @param name variable name.
     * @param data variable data.
     * @return Error.
     */
    virtual Error WriteGlobalGuidVariable(const std::string& name, const std::vector<uint8_t>& data) = 0;

    /**
     * Returns partition UUID by EFI variable name.
     *
     * @param partitionUUID partition UUID.
     * @return RetWithError<std::string>.
     */
    virtual RetWithError<std::string> GetPartUUID(const std::string& efiVarName) const = 0;

    /**
     * Returns all EFI variable names.
     *
     * @return RetWithError<std::vector<std::string>>.
     */
    virtual RetWithError<std::vector<std::string>> GetAllVariables() const = 0;

    /**
     * Creates a new EFI boot entry.
     *
     * @param parentDevice parent device path.
     * @param partition partition number.
     * @param loaderPath bootloader path.
     * @param bootID desired boot entry ID.
     * @return Error.
     */
    virtual Error CreateBootEntry(
        const std::string& parentDevice, int partition, const std::string& loaderPath, uint16_t bootID)
        = 0;
};

} // namespace aos::sm::launcher

#endif
