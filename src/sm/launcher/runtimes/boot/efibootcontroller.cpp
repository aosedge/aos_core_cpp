/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <charconv>
#include <filesystem>
#include <fstream>
#include <regex>

#include <core/common/tools/logger.hpp>

#include <common/utils/utils.hpp>

#include "efibootcontroller.hpp"
#include "efivar.hpp"

namespace aos::sm::launcher {

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

    std::transform(mBootItems.begin(), mBootItems.end(), std::back_inserter(devices),
        [](const BootItem& item) { return item.mDevice; });

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

    if (auto err = mEFIVar->WriteGlobalGuidVariable(cBootNextName, ToUint8({bootID})); !err.IsNone()) {
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

    err = UpdateBootOrder({currentBootID}, bootCurrent);
    if (!err.IsNone()) {
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

    const std::regex cRegEx(cBootItemPattern);

    std::vector<EFIBootController::BootItem> bootItems;

    auto [efiVariables, err] = mEFIVar->GetAllVariables();
    if (!err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    for (const auto& efiVariable : efiVariables) {
        std::smatch match;
        if (!std::regex_search(efiVariable, match, cRegEx) || match.size() != 3) {
            continue;
        }

        const auto hexBootID = match[2].str();

        BootItem item;

        Tie(item.mID, err) = ConvertHex(hexBootID);

        if (!err.IsNone()) {
            LOG_DBG() << "Failed to convert EFI boot ID from hex string" << AOS_ERROR_WRAP(err);

            continue;
        }

        Tie(item.mPartitionUUID, err) = mEFIVar->GetPartUUID(efiVariable);
        if (!err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
            LOG_ERR() << "EFI boot entry has no associated partition UUID" << Log::Field("bootID", item.mID);

            continue;
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

    err = SetPartitionIDs();
    if (!err.IsNone()) {
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

    err = UpdateBootOrder(newBootIDs);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
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

Error EFIBootController::UpdateBootOrder(const std::vector<uint16_t>& newBootIDs)
{
    if (newBootIDs.empty()) {
        return ErrorEnum::eNone;
    }

    auto [oldBootOrder, err] = GetBootOrder();
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return UpdateBootOrder(newBootIDs, oldBootOrder);
}

Error EFIBootController::UpdateBootOrder(
    const std::vector<uint16_t>& newBootIDs, const std::vector<uint16_t>& oldBootOrder)
{
    std::vector<uint16_t> newBootOrder = newBootIDs;

    std::copy_if(oldBootOrder.begin(), oldBootOrder.end(), std::back_inserter(newBootOrder),
        [&newBootIDs](uint16_t id) { return std::find(newBootIDs.begin(), newBootIDs.end(), id) == newBootIDs.end(); });

    if (newBootOrder == oldBootOrder) {
        LOG_DBG() << "Boot order is up to date, nothing to do";

        return ErrorEnum::eNone;
    }

    return mEFIVar->WriteGlobalGuidVariable(cBootOrderName, ToUint8(newBootOrder));
}

}; // namespace aos::sm::launcher
