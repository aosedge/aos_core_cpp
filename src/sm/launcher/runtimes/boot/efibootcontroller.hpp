/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_EFIBOOTCONTROLLER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_EFIBOOTCONTROLLER_HPP_

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
    static constexpr auto cDefaultLoader   = "/EFI/BOOT/bootx64.efi";
    static constexpr auto cBootItemPattern = "(^Boot)([0-9A-Fa-f]{4})$";
    static constexpr auto cBootOrderName   = "BootOrder";
    static constexpr auto cBootCurrentName = "BootCurrent";
    static constexpr auto cBootNextName    = "BootNext";

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
    Error                                        UpdateBootOrder(const std::vector<uint16_t>& newBootIDs);
    Error UpdateBootOrder(const std::vector<uint16_t>& newBootIDs, const std::vector<uint16_t>& oldBootOrder);

    mutable std::mutex                   mMutex;
    std::shared_ptr<PartitionManagerItf> mPartitionManager;
    std::shared_ptr<EFIVarItf>           mEFIVar;
    BootConfig                           mConfig;
    std::vector<BootItem>                mBootItems;
};

} // namespace aos::sm::launcher

#endif
