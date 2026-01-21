/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_EFICONTROLLER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_EFICONTROLLER_HPP_

#include <mutex>
#include <string>
#include <vector>

#include <core/common/types/common.hpp>

#include "config.hpp"
#include "itf/bootcontroller.hpp"
#include "itf/efivar.hpp"
#include "partitionmanager.hpp"

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
     * @param attributes variable attributes.
     * @param mode write mode.
     * @return Error.
     */
    Error WriteGlobalGuidVariable(
        const std::string& name, const std::vector<uint8_t>& data, uint32_t attributes, mode_t mode) override;

    /**
     * @brief Returns partition UUID by EFI variable name.
     *
     * @param partitionUUID partition UUID.
     * @return RetWithError<std::string>.
     */
    RetWithError<std::string> GetPartUUID(const std::string& efiVarName) const override;

    /**
     * @brief Returns all EFI variable names.
     *
     * @return RetWithError<std::vector<std::string>>.
     */
    RetWithError<std::vector<std::string>> GetAllVariables() const override;

    /**
     * @brief Creates a new EFI boot entry.
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
    static constexpr auto cWriteAttribute      = 0600;
    static constexpr auto cBootVarPrefix       = "Boot";

    std::string CreateBootVariableName(uint16_t bootID) const;
};

/**
 * EFI boot controller.
 */
class EFIBootController : public BootControllerItf {
public:
    /**
     * Initializes boot controller.
     *
     * @param config boot config.
     * @return Error.
     */
    Error Init(const BootConfig& config) override;

    /**
     * Returns boot partition devices.
     *
     * @param[out] devices boot partition devices.
     * @return Error.
     */
    Error GetPartitionDevices(std::vector<std::string>& devices) const override;

    /**
     * Returns current boot index.
     *
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetCurrentBoot() const override;

    /**
     * Returns main boot index.
     *
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetMainBoot() const override;

    /**
     * Sets the main boot partition index.
     *
     * @param index main boot partition index.
     * @return Error.
     */
    Error SetMainBoot(size_t index) override;

    /**
     * Sets boot successful flag.
     *
     * @return Error.
     */
    Error SetBootOK() override;

private:
    static constexpr auto cDefaultLoader       = "/EFI/BOOT/bootx64.efi";
    static constexpr auto cBootItemNamePattern = "^Boot[0-9A-Fa-f]{4}$";
    static constexpr auto cBootItemIDPattern   = "[0-9A-Fa-f]{4}$";
    static constexpr auto cGlobalGUID          = "8be4df61-93ca-11d2-aa0d-00e098032b8c";
    static constexpr auto cBootOrderName       = "BootOrder";
    static constexpr auto cBootCurrentName     = "BootCurrent";
    static constexpr auto cBootNextName        = "BootNext";
    static constexpr auto cWriteAttribute      = 0600;

    struct BootItem {
        uint16_t    mID {};
        std::string mDevice;
        std::string mParentDevice;
        int         mPartitionNumber {};
        std::string mPartitionUUID;

        friend Log& operator<<(Log& log, const BootItem& item)
        {
            log << Log::Field("id", item.mID) << Log::Field("device", item.mDevice.c_str())
                << Log::Field("parentDevice", item.mParentDevice.c_str())
                << Log::Field("partitionNumber", item.mPartitionNumber)
                << Log::Field("partitionUUID", item.mPartitionUUID.c_str());

            return log;
        }
    };

    virtual std::shared_ptr<EFIVarItf>           CreateEFIVar() const;
    virtual std::shared_ptr<PartitionManagerItf> CreatePartitionManager() const;
    RetWithError<std::vector<BootItem>>          ReadBootEntries();
    RetWithError<uint16_t>                       ConvertHex(const std::string& hexStr) const;
    Error                                        InitBootPartitions(const BootConfig& config);
    Error                                        SetPartitionIDs();
    RetWithError<std::string>                    GetPartitionPrefix() const;
    RetWithError<std::vector<uint16_t>>          GetBootOrder() const;
    RetWithError<uint16_t>                       GetBootCurrent() const;
    std::string                                  GetLoaderPath() const;
    RetWithError<std::vector<uint16_t>>          ReadVariable(const std::string& name) const;
    std::vector<uint16_t>                        ToUint16(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t>                         ToUint8(const std::vector<uint16_t>& data) const;

    mutable std::mutex                   mMutex;
    std::shared_ptr<PartitionManagerItf> mPartitionManager;
    std::shared_ptr<EFIVarItf>           mEFIVar;
    BootConfig                           mConfig;
    std::vector<BootItem>                mBootItems;
};

} // namespace aos::sm::launcher

#endif
