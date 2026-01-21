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
}

#include <core/common/tools/logger.hpp>

#include <common/utils/utils.hpp>

#include "efivar.hpp"

namespace aos::sm::launcher {

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

Error EFIVar::WriteGlobalGuidVariable(const std::string& name, const std::vector<uint8_t>& data)
{
    auto out = data;

    if (efi_set_variable(EFI_GLOBAL_GUID, name.c_str(), out.data(), out.size(), cEFIVarAttributes, cWriteMode) < 0) {
        uint32_t idx       = 0;
        char*    file      = nullptr;
        char*    func      = nullptr;
        int      line      = 0;
        char*    message   = nullptr;
        int      error_num = 0;

        Error err = Error(ErrorEnum::eFailed, "can't write EFI variable");

        while (efi_error_get(idx++, &file, &func, &line, &message, &error_num) == 1) {
            LOG_DBG() << "Fetched EFI error" << Log::Field("file", file) << Log::Field("function", func)
                      << Log::Field("line", line) << Log::Field("message", message)
                      << Log::Field("errorNum", error_num);

            err = Error(ErrorEnum::eFailed, message);
        }

        return AOS_ERROR_WRAP(err);
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
            const auto dpHD = reinterpret_cast<const efidp_hd*>(next);

            if (dpHD->signature_type != cHDSignatureGUIDType) {
                continue;
            }

            char*      uuidStr = nullptr;
            const auto guid    = reinterpret_cast<const efi_guid_t*>(&dpHD->signature[0]);

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

    if (auto err = WriteGlobalGuidVariable(bootVarName, efiLoadOpt); !err.IsNone()) {
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

}; // namespace aos::sm::launcher
