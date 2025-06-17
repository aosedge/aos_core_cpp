/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ALERTS_MOCK_HPP_
#define ALERTS_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/common/alerts/alerts.hpp>

namespace aos::sm::alerts {

class StorageMock : public StorageItf {
public:
    MOCK_METHOD(Error, SetJournalCursor, (const String& cursor), (override));
    MOCK_METHOD(Error, GetJournalCursor, (String & cursor), (const, override));
};

class InstanceInfoProviderMock : public InstanceInfoProviderItf {
public:
    MOCK_METHOD(RetWithError<ServiceInstanceData>, GetInstanceInfoByID, (const String& string), (override));
};

} // namespace aos::sm::alerts

namespace aos::alerts {

class SenderMock : public SenderItf {
public:
    MOCK_METHOD(Error, SendAlert, (const cloudprotocol::AlertVariant& alert), (override));
};

} // namespace aos::alerts

#endif // ALERTS_MOCK_HPP_
