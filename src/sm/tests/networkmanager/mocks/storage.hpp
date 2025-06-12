/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <aos/sm/networkmanager.hpp>

class MockStorage : public aos::sm::networkmanager::StorageItf {
public:
    MOCK_METHOD(aos::Error, GetTrafficMonitorData, (const aos::String& chain, aos::Time& time, uint64_t& value),
        (const, override));
    MOCK_METHOD(aos::Error, SetTrafficMonitorData, (const aos::String& chain, const aos::Time& time, uint64_t value),
        (override));
    MOCK_METHOD(aos::Error, RemoveNetworkInfo, (const aos::String& networkID), (override));
    MOCK_METHOD(aos::Error, AddNetworkInfo, (const aos::sm::networkmanager::NetworkInfo& info), (override));
    MOCK_METHOD(
        aos::Error, GetNetworksInfo, (aos::Array<aos::sm::networkmanager::NetworkInfo> & networks), (const, override));
    MOCK_METHOD(aos::Error, RemoveTrafficMonitorData, (const aos::String& chain), (override));
};
