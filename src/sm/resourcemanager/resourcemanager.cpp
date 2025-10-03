/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <common/utils/exception.hpp>
#include <sm/logger/logmodule.hpp>

#include "resourcemanager.hpp"

namespace aos::sm::resourcemanager {

/***********************************************************************************************************************
 * HostDeviceManager
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error HostDeviceManager::Init()
{
    try {
        for (const auto& entry : std::filesystem::directory_iterator(cDevicesDirectory)) {
            mDevices.insert(entry.path().string());
        }

        if (auto err = ParseGroups(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error HostDeviceManager::CheckDevice(const String& device) const
{
    StaticArray<StaticString<cFilePathLen>, 2> devices;

    if (auto err = device.Split(devices, ':'); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (devices.IsEmpty()) {
        return AOS_ERROR_WRAP(ErrorEnum::eFailed);
    }

    if (mDevices.find(devices[0].CStr()) == mDevices.end()) {
        return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
    }

    return ErrorEnum::eNone;
}

Error HostDeviceManager::CheckGroup(const String& group) const
{
    auto it = mGroups.find(group.CStr());

    return it != mGroups.end() ? ErrorEnum::eNone : ErrorEnum::eNotFound;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error HostDeviceManager::ParseGroups()
{
    std::ifstream groupFile(cGroupsFile);

    if (!groupFile.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to open group file"));
    }

    std::string line;

    while (std::getline(groupFile, line)) {
        // Each line is in the format: group_name:x:group_id:group_members
        std::istringstream lineStream(line);
        std::string        groupName;

        // Read the first field (group name) before the first colon
        if (std::getline(lineStream, groupName, ':')) {
            if (groupName.empty() || groupName[0] == '#') {
                continue;
            }

            mGroups.insert(groupName);
        }
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::resourcemanager
