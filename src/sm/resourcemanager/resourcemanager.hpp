/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_RESOURCEMANAGER_RESOURCEMANAGER_HPP_
#define AOS_SM_RESOURCEMANAGER_RESOURCEMANAGER_HPP_

#include <set>
#include <string>
#include <vector>

#include <core/sm/resourcemanager/resourcemanager.hpp>

namespace aos::sm::resourcemanager {

/**
 * Host device manager.
 */
class HostDeviceManager : public sm::resourcemanager::HostDeviceManagerItf {
public:
    /**
     * Initializes host device manager object.
     *
     * @return Error.
     */
    Error Init();

    /**
     * Checks if device exists.
     *
     * @param device device name.
     * @return true if device exists, false otherwise.
     */
    Error CheckDevice(const String& device) const override;

    /**
     * Checks if group exists.
     *
     * @param group group name.
     * @return true if group exists, false otherwise.
     */
    Error CheckGroup(const String& group) const override;

private:
    static constexpr auto cDevicesDirectory = "/dev/";
    static constexpr auto cGroupsFile       = "/etc/group";

    Error ParseGroups();

    std::set<std::string> mDevices;
    std::set<std::string> mGroups;
};

} // namespace aos::sm::resourcemanager

#endif
