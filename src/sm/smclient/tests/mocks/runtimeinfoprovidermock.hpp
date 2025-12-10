/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_SMCLIENT_TESTS_MOCKS_RUNTIMEINFOPROVIDERMOCK_HPP_
#define AOS_SM_SMCLIENT_TESTS_MOCKS_RUNTIMEINFOPROVIDERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/sm/launcher/itf/runtimeinfoprovider.hpp>

namespace aos::sm::launcher {

class RuntimeInfoProviderMock : public RuntimeInfoProviderItf {
public:
    MOCK_METHOD(Error, GetRuntimesInfos, (Array<RuntimeInfo>&), (const, override));
};

} // namespace aos::sm::launcher

#endif
