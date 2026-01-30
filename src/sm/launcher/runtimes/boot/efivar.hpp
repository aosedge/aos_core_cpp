/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_EFICONTROLLER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_EFICONTROLLER_HPP_

#include <efivar/efivar.h>

#include "itf/efivar.hpp"

namespace aos::sm::launcher {

/**
 * EFI variable.
 */
class EFIVar : public EFIVarItf {
public:
    /**
     * Reads EFI variable.
     *
     * @param name variable name.
     * @param data[out] variable data.
     * @param attributes[out] variable attributes.
     * @return Error.
     */
    virtual Error ReadVariable(
        const std::string& name, std::vector<uint8_t>& data, uint32_t& attributes) const override;

    /**
     * Writes EFI global GUID variable.
     *
     * @param name variable name.
     * @param data variable data.
     * @return Error.
     */
    Error WriteGlobalGuidVariable(const std::string& name, const std::vector<uint8_t>& data) override;

    /**
     * Returns partition UUID by EFI variable name.
     *
     * @param partitionUUID partition UUID.
     * @return RetWithError<std::string>.
     */
    RetWithError<std::string> GetPartUUID(const std::string& efiVarName) const override;

    /**
     * Returns all EFI variable names.
     *
     * @return RetWithError<std::vector<std::string>>.
     */
    RetWithError<std::vector<std::string>> GetAllVariables() const override;

    /**
     * Creates a new EFI boot entry.
     *
     * @param parentDevice parent device path.
     * @param partition partition number.
     * @param loaderPath bootloader path.
     * @param bootID desired boot entry ID.
     * @return Error.
     */
    Error CreateBootEntry(
        const std::string& parentDevice, int partition, const std::string& loaderPath, uint16_t bootID) override;

private:
    static constexpr auto cDPHeaderSize        = 4;
    static constexpr auto cHDSignatureGUIDType = 2;
    static constexpr auto cEFIBootAbbrevHD     = 2;
    static constexpr auto cEDDDefaultDevice    = 0x80;
    static constexpr auto cWriteMode           = 0600;
    static constexpr auto cBootVarPrefix       = "Boot";
    static constexpr auto cEFIVarAttributes
        = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

    std::string CreateBootVariableName(uint16_t bootID) const;
};

} // namespace aos::sm::launcher

#endif
