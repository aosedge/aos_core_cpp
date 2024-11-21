/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RESOURCEMANAGER_MOCK_HPP_
#define RESOURCEMANAGER_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/sm/resourcemanager.hpp>

namespace aos::sm::resourcemanager {

/**
 * Resource manager mock.
 */
class ResourceManagerMock : public aos::sm::resourcemanager::ResourceManagerItf {
public:
    MOCK_METHOD(RetWithError<StaticString<cVersionLen>>, GetNodeConfigVersion, (), (const, override));
    MOCK_METHOD(Error, GetNodeConfig, (aos::NodeConfig&), (const, override));
    MOCK_METHOD(Error, GetDeviceInfo, (const String&, DeviceInfo&), (const, override));
    MOCK_METHOD(Error, GetResourceInfo, (const String&, ResourceInfo&), (const, override));
    MOCK_METHOD(Error, AllocateDevice, (const String&, const String&), (override));
    MOCK_METHOD(Error, ReleaseDevice, (const String&, const String&), (override));
    MOCK_METHOD(Error, ReleaseDevices, (const String&), (override));
    MOCK_METHOD(Error, ResetAllocatedDevices, (), (override));
    MOCK_METHOD(Error, GetDeviceInstances, (const String&, Array<StaticString<cInstanceIDLen>>&), (const override));
    MOCK_METHOD(Error, CheckNodeConfig, (const String&, const String&), (const override));
    MOCK_METHOD(Error, UpdateNodeConfig, (const String&, const String&), (override));
    MOCK_METHOD(Error, SubscribeCurrentNodeConfigChange, (NodeConfigReceiverItf&), (override));
    MOCK_METHOD(Error, UnsubscribeCurrentNodeConfigChange, (NodeConfigReceiverItf&), (override));
};

} // namespace aos::sm::resourcemanager

#endif
