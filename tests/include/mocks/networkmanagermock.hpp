/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETWORKMANAGER_MOCK_HPP_
#define NETWORKMANAGER_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/sm/networkmanager.hpp>

/**
 * Network manager mock.
 */
class NetworkManagerMock : public aos::sm::networkmanager::NetworkManagerItf {
public:
    MOCK_METHOD(aos::Error, GetNetnsPath, (const aos::String&, aos::String&), (const override));
    MOCK_METHOD(aos::Error, UpdateNetworks, (const aos::Array<aos::NetworkParameters>&), (override));
    MOCK_METHOD(aos::Error, AddInstanceToNetwork,
        (const aos::String&, const aos::String&, const aos::sm::networkmanager::NetworkParams&), (override));
    MOCK_METHOD(aos::Error, RemoveInstanceFromNetwork, (const aos::String&, const aos::String&), (override));
    MOCK_METHOD(aos::Error, GetInstanceIP, (const aos::String&, const aos::String&, aos::String&), (const override));
    MOCK_METHOD(aos::Error, GetInstanceTraffic, (const aos::String&, uint64_t&, uint64_t&), (const override));
    MOCK_METHOD(aos::Error, SetTrafficPeriod, (uint32_t), (override));
};

#endif
