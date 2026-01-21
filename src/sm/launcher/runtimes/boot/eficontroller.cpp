/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <charconv>
#include <filesystem>
#include <fstream>
#include <regex>

extern "C" {
#include <efivar/efiboot.h>
#include <efivar/efivar.h>

#include <efivar/efiboot-loadopt.h>
}

#include <core/common/tools/logger.hpp>

#include <common/utils/utils.hpp>

#include "eficontroller.hpp"

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cEFIVarAttributes
    = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

} // namespace

/***********************************************************************************************************************
 * EFIVar
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error EFIVar::ReadVariable(const std::string& name, std::vector<uint8_t>& data, uint32_t& attributes) const
{
    uint8_t* efiData = nullptr;
    auto     cleanup = DeferRelease(&efiData, [](uint8_t** ptr) {
        if (ptr) {
            free(*ptr);
        }
    });

    size_t efiDataSize = 0;

    if (efi_get_variable(EFI_GLOBAL_GUID, name.c_str(), &efiData, &efiDataSize, &attributes) < 0) {
        return Error(ErrorEnum::eFailed, strerror(errno));
    }

    data.assign(efiData, efiData + efiDataSize);

    return ErrorEnum::eNone;
}

Error EFIVar::WriteGlobalGuidVariable(
    const std::string& name, const std::vector<uint8_t>& data, uint32_t attributes, mode_t mode)
{
    if (efi_set_variable(EFI_GLOBAL_GUID, name.c_str(), data.data(), data.size(), attributes, mode) < 0) {
        uint32_t idx       = 0;
        char*    file      = nullptr;
        char*    func      = nullptr;
        int      line      = 0;
        char*    message   = nullptr;
        int      error_num = 0;

        while (efi_error_get(idx++, &file, &func, &line, &message, &error_num) == 1) {
            LOG_DBG() << "EFI set variable error" << Log::Field("file", file) << Log::Field("function", func)
                      << Log::Field("line", line) << Log::Field("message", message)
                      << Log::Field("errorNum", error_num);
        }

        return Error(ErrorEnum::eFailed, strerror(errno));
    }

    return ErrorEnum::eNone;
}

RetWithError<std::string> EFIVar::GetPartUUID(const std::string& efiVarName) const
{
    LOG_DBG() << "Get partition UUID from EFI variable" << Log::Field("varName", efiVarName.c_str());

    std::vector<uint8_t> data;
    uint32_t             attributes = 0;

    if (auto err = ReadVariable(efiVarName, data, attributes); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    if (data.empty()) {
        return {{}, AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound))};
    }

    const auto loadOpt = reinterpret_cast<efi_load_option*>(static_cast<uint8_t*>(data.data()));
    const auto len     = efi_loadopt_pathlen(loadOpt, data.size());
    auto       dpData  = efi_loadopt_path(loadOpt, data.size());

    if (!efidp_is_valid(dpData, len)) {
        return {{}, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)))};
    }

    for (const_efidp next = dpData; next != nullptr;) {
        const auto type    = next->type;
        const auto subType = next->subtype;
        const auto length  = next->length;

        if (length < sizeof(efidp_header)) {
            return {{}, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)))};
        }

        if (type == EFIDP_END_TYPE && subType == EFIDP_END_ENTIRE) {
            break;
        }

        if (type == EFIDP_MEDIA_TYPE && subType == EFIDP_MEDIA_HD) {
            const auto data = reinterpret_cast<const efidp_hd*>(next);

            if (data->signature_type != cHDSignatureGUIDType) {
                continue;
            }

            char*      uuidStr = nullptr;
            const auto guid    = reinterpret_cast<const efi_guid_t*>(&data->signature[0]);

            if (!efi_guid_to_str(guid, &uuidStr)) {
                return {{}, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)))};
            }

            return std::string(uuidStr);
        }

        if (!efidp_next_node(next, &next)) {
            return {{}, AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, strerror(errno)))};
        }
    }

    return {{}, AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "partition UUID not found"))};
}

RetWithError<std::vector<std::string>> EFIVar::GetAllVariables() const
{
    std::vector<std::string> result;

    efi_guid_t* guid = nullptr;
    char*       name = nullptr;

    while (efi_get_next_variable_name(&guid, &name) != 0) {
        if (!guid || !name) {
            return {{}, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to get EFI variable name"))};
        }

        result.emplace_back(name);
    }

    return result;
}

Error EFIVar::CreateBootEntry(
    const std::string& parentDevice, int partition, const std::string& loaderPath, uint16_t bootID)
{
    // Get required vector size to store EFI device path, then generate it.

    const auto efiDPSize = efi_generate_file_device_path_from_esp(
        nullptr, 0, parentDevice.c_str(), partition, loaderPath.c_str(), cEFIBootAbbrevHD, cEDDDefaultDevice);
    if (efiDPSize < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    std::vector<uint8_t> efiDP(static_cast<size_t>(efiDPSize));

    if (efi_generate_file_device_path_from_esp(efiDP.data(), efiDP.size(), parentDevice.c_str(), partition,
            loaderPath.c_str(), cEFIBootAbbrevHD, cEDDDefaultDevice)
        < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    auto bootVarName = CreateBootVariableName(bootID);

    auto efiLoadOptSize = efi_loadopt_create(nullptr, 0, 1, reinterpret_cast<efidp>(efiDP.data()), efiDP.size(),
        reinterpret_cast<unsigned char*>(bootVarName.data()), nullptr, 0);
    if (efiLoadOptSize < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    std::vector<uint8_t> efiLoadOpt(static_cast<size_t>(efiLoadOptSize));

    if (efi_loadopt_create(efiLoadOpt.data(), efiLoadOpt.size(), 1, reinterpret_cast<efidp>(efiDP.data()), efiDP.size(),
            reinterpret_cast<unsigned char*>(bootVarName.data()), nullptr, 0)
        < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    if (auto err = WriteGlobalGuidVariable(bootVarName, efiLoadOpt, cEFIVarAttributes, cWriteAttribute);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Created EFI boot entry" << Log::Field("bootVarName", bootVarName.c_str());

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::string EFIVar::CreateBootVariableName(uint16_t bootID) const
{
    std::stringstream ss;

    ss << cBootVarPrefix << std::hex << std::setw(4) << std::setfill('0') << bootID;

    return ss.str();
}

/***********************************************************************************************************************
 * EFIBootController
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error EFIBootController::Init(const BootConfig& config)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Init EFI boot controller";

    mConfig = config;

    mPartitionManager = CreatePartitionManager();
    mEFIVar           = CreateEFIVar();

    if (auto err = InitBootPartitions(config); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& bootItem : mBootItems) {
        LOG_DBG() << "Configured boot item" << bootItem;
    }

    return ErrorEnum::eNone;
}

Error EFIBootController::GetPartitionDevices(std::vector<std::string>& devices) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get boot partition devices" << Log::Field("count", mBootItems.size());

    for (const auto& bootItem : mBootItems) {
        devices.push_back(bootItem.mDevice);
    }

    return ErrorEnum::eNone;
}

RetWithError<size_t> EFIBootController::GetCurrentBoot() const
{
    std::lock_guard lock {mMutex};

    auto [efiCurrentBoot, err] = GetBootCurrent();
    if (!err.IsNone()) {
        return {0, AOS_ERROR_WRAP(err)};
    }

    LOG_DBG() << "Get EFI current boot" << Log::Field("bootID", efiCurrentBoot);

    auto itBootItem = std::find_if(mBootItems.begin(), mBootItems.end(),
        [efiCurrentBoot](const BootItem& item) { return item.mID == efiCurrentBoot; });

    if (itBootItem == mBootItems.end()) {
        LOG_WRN() << "Boot from an unknown partition" << Log::Field("bootID", efiCurrentBoot);

        return 0;
    }

    return std::distance(mBootItems.begin(), itBootItem);
}

RetWithError<size_t> EFIBootController::GetMainBoot() const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get main boot";

    auto [currentBootOrder, err] = GetBootOrder();
    if (!err.IsNone()) {
        return {0, AOS_ERROR_WRAP(err)};
    }

    if (currentBootOrder.empty()) {
        return {0, Error(ErrorEnum::eNotFound, "boot order is empty")};
    }

    auto itBootItem = std::find_if(mBootItems.begin(), mBootItems.end(),
        [first = currentBootOrder[0]](const BootItem& item) { return item.mID == first; });

    if (itBootItem == mBootItems.end()) {
        return {0, AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "current boot entry not found"))};
    }

    return std::distance(mBootItems.begin(), itBootItem);
}

Error EFIBootController::SetMainBoot(size_t index)
{
    std::lock_guard lock {mMutex};

    if (mBootItems.size() <= index) {
        LOG_DBG() << "Set main boot" << Log::Field("index", index);

        return Error(ErrorEnum::eOutOfRange, "wrong main boot index");
    }

    const auto bootID = mBootItems[index].mID;

    LOG_DBG() << "Set main boot" << Log::Field("index", index) << Log::Field("bootID", bootID);

    if (auto err
        = mEFIVar->WriteGlobalGuidVariable(cBootNextName, ToUint8({bootID}), cEFIVarAttributes, cWriteAttribute);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error EFIBootController::SetBootOK()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Set boot OK";

    auto [bootCurrent, err] = GetBootOrder();
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    uint16_t currentBootID = 0;

    Tie(currentBootID, err) = GetBootCurrent();
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (std::find_if(mBootItems.begin(), mBootItems.end(),
            [currentBootID](const BootItem& item) { return item.mID == currentBootID; })
        == mBootItems.end()) {
        LOG_DBG() << "Current boot partition is not in configured ones" << Log::Field("currentBootID", currentBootID);

        return ErrorEnum::eNone;
    }

    if (std::find(bootCurrent.begin(), bootCurrent.end(), currentBootID) == bootCurrent.end()) {
        LOG_WRN() << "Current boot ID not found in boot order, nothing to do"
                  << Log::Field("currentBootID", currentBootID);

        return ErrorEnum::eNone;
    }

    if (currentBootID == bootCurrent[0]) {
        LOG_DBG() << "Current boot is already main boot, nothing to do" << Log::Field("currentBootID", currentBootID);

        return ErrorEnum::eNone;
    }

    std::vector<uint16_t> newBootOrder;
    newBootOrder.push_back(currentBootID);

    for (const auto& id : bootCurrent) {
        if (id != currentBootID) {
            newBootOrder.push_back(id);
        }
    }

    if (auto err
        = mEFIVar->WriteGlobalGuidVariable(cBootOrderName, ToUint8(newBootOrder), cEFIVarAttributes, cWriteAttribute);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::shared_ptr<EFIVarItf> EFIBootController::CreateEFIVar() const
{
    return std::make_shared<EFIVar>();
}

std::shared_ptr<PartitionManagerItf> EFIBootController::CreatePartitionManager() const
{
    return std::make_shared<PartitionManager>();
}

RetWithError<std::vector<EFIBootController::BootItem>> EFIBootController::ReadBootEntries()
{
    LOG_DBG() << "Read EFI boot entries";

    const std::regex cRegEx(cBootItemIDPattern);

    std::vector<EFIBootController::BootItem> bootItems;

    auto [efiVariables, err] = mEFIVar->GetAllVariables();
    if (!err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    for (const auto& efiVariable : efiVariables) {
        if (!std::regex_match(efiVariable, std::regex(cBootItemNamePattern))) {
            continue;
        }

        std::smatch match;
        if (!std::regex_search(efiVariable, match, cRegEx)) {
            continue;
        }

        LOG_DBG() << "Read EFI boot variable" << Log::Field("varName", efiVariable.c_str());

        const auto hexBootID = match[0].str();

        BootItem item;

        Tie(item.mID, err) = ConvertHex(hexBootID);

        if (!err.IsNone()) {
            LOG_DBG() << "Failed to convert EFI boot ID from hex string" << AOS_ERROR_WRAP(err);

            continue;
        }

        Tie(item.mPartitionUUID, err) = mEFIVar->GetPartUUID(efiVariable);
        if (!err.IsNone()) {
            if (!err.Is(ErrorEnum::eNotFound)) {
                LOG_ERR() << "EFI boot entry has no associated partition UUID" << Log::Field("bootID", item.mID);

                continue;
            }
        }

        bootItems.push_back(std::move(item));
    }

    std::sort(bootItems.begin(), bootItems.end(), [](const BootItem& a, const BootItem& b) { return a.mID < b.mID; });

    return bootItems;
}

RetWithError<uint16_t> EFIBootController::ConvertHex(const std::string& hexStr) const
{
    uint16_t result = 0;

    const auto res = std::from_chars(hexStr.data(), hexStr.data() + hexStr.size(), result, 16);
    if (res.ec != std::errc()) {
        return {0, Error(ErrorEnum::eInvalidArgument, "invalid hex string")};
    }

    return result;
}

Error EFIBootController::InitBootPartitions(const BootConfig& config)
{
    Error       err = ErrorEnum::eNone;
    std::string partitionPrefix;

    if (config.mDetectMode == BootDetectModeEnum::eAuto) {
        Tie(partitionPrefix, err) = GetPartitionPrefix();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    for (const auto& partition : config.mPartitions) {
        const auto device = partitionPrefix + partition;

        PartInfo partInfo;

        err = mPartitionManager->GetPartInfo(device, partInfo);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        BootItem item;
        item.mDevice          = device;
        item.mParentDevice    = partInfo.mParentDevice;
        item.mPartitionNumber = partInfo.mPartitionNumber;
        item.mPartitionUUID   = partInfo.mPartUUID;

        mBootItems.push_back(std::move(item));
    }

    if (auto err = SetPartitionIDs(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error EFIBootController::SetPartitionIDs()
{
    auto [efiBootItems, err] = ReadBootEntries();
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    size_t nextAvailableID = efiBootItems.empty() ? 0 : efiBootItems.back().mID + 1;

    std::vector<uint16_t> newBootIDs;

    for (auto& bootItem : mBootItems) {
        if (auto it = std::find_if(efiBootItems.begin(), efiBootItems.end(),
                [&bootItem](const BootItem& item) { return item.mPartitionUUID == bootItem.mPartitionUUID; });
            it != efiBootItems.end()) {
            bootItem.mID = it->mID;

            continue;
        }

        bootItem.mID = nextAvailableID++;

        err = mEFIVar->CreateBootEntry(
            bootItem.mParentDevice, bootItem.mPartitionNumber, GetLoaderPath(), bootItem.mID);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        newBootIDs.push_back(bootItem.mID);

        LOG_DBG() << "Created new boot entry" << Log::Field("item", bootItem);
    }

    if (!newBootIDs.empty()) {
        LOG_DBG() << "Update boot order with new boot entries";

        std::vector<uint16_t> bootOrder;
        Tie(bootOrder, err) = GetBootOrder();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        bootOrder.insert(bootOrder.begin(), newBootIDs.begin(), newBootIDs.end());

        err = mEFIVar->WriteGlobalGuidVariable(cBootOrderName, ToUint8(bootOrder), cEFIVarAttributes, cWriteAttribute);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

RetWithError<std::vector<uint16_t>> EFIBootController::GetBootOrder() const
{
    auto [result, err] = ReadVariable(cBootOrderName);
    if (!err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    return result;
}

RetWithError<uint16_t> EFIBootController::GetBootCurrent() const
{
    auto [result, err] = ReadVariable(cBootCurrentName);
    if (!err.IsNone()) {
        return {0, AOS_ERROR_WRAP(err)};
    }

    if (result.size() != 1) {
        return {0, Error(ErrorEnum::eInvalidArgument, "invalid variable size")};
    }

    return result[0];
}

RetWithError<std::string> EFIBootController::GetPartitionPrefix() const
{
    LOG_DBG() << "Get partition prefix from /proc/cmdline";

    std::ifstream file("/proc/cmdline");
    if (!file.is_open()) {
        return {"", Error(ErrorEnum::eFailed, "can't open /proc/cmdline")};
    }

    std::string cmdline;
    std::getline(file, cmdline);

    std::regex  rootRegex(R"(root=([^ \t]+))");
    std::smatch match;

    if (!std::regex_search(cmdline, match, rootRegex)) {
        return {"", Error(ErrorEnum::eNotFound, "root device not found in /proc/cmdline")};
    }

    std::string device = match[1];

    while (!device.empty() && std::isdigit(device.back())) {
        device.pop_back();
    }

    return device;
}

std::string EFIBootController::GetLoaderPath() const
{
    return mConfig.mLoader.empty() ? cDefaultLoader : mConfig.mLoader;
}

RetWithError<std::vector<uint16_t>> EFIBootController::ReadVariable(const std::string& name) const
{
    std::vector<uint8_t> data;
    uint32_t             attributes = 0;

    if (auto err = mEFIVar->ReadVariable(name, data, attributes); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    return ToUint16(data);
}

std::vector<uint16_t> EFIBootController::ToUint16(const std::vector<uint8_t>& data) const
{
    return std::vector<uint16_t> {
        reinterpret_cast<const uint16_t*>(data.data()), reinterpret_cast<const uint16_t*>(data.data() + data.size())};
}

std::vector<uint8_t> EFIBootController::ToUint8(const std::vector<uint16_t>& data) const
{
    return std::vector<uint8_t> {reinterpret_cast<const uint8_t*>(data.data()),
        reinterpret_cast<const uint8_t*>(data.data()) + data.size() * sizeof(uint16_t)};
}

}; // namespace aos::sm::launcher
