/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_TESTS_MOCKS_RUNTIMESTORAGEMOCK_HPP_
#define AOS_SM_TESTS_MOCKS_RUNTIMESTORAGEMOCK_HPP_

#include <gmock/gmock.h>

#include <sm/launcher/runtimes/itf/storage.hpp>

namespace aos::sm::launcher::runtimes {

class StorageMock : public StorageItf {
public:
    MOCK_METHOD(Error, AddRuntimeInstance, (const RuntimeInstanceData&), (override));
    MOCK_METHOD(Error, UpdateRuntimeInstance, (const RuntimeInstanceData&), (override));
    MOCK_METHOD(Error, RemoveRuntimeInstance, (const String&, const InstanceIdent&, const String&), (override));
    MOCK_METHOD(Error, GetRuntimeInstances, (const String&, Array<RuntimeInstanceData>&), (const, override));
};

} // namespace aos::sm::launcher::runtimes

#endif
