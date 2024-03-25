/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PERMHANDLER_ITF_MOCK_HPP_
#define PERMHANDLER_ITF_MOCK_HPP_

#include <aos/iam/permhandler.hpp>
#include <gmock/gmock.h>

namespace aos {
namespace iam {
namespace permhandler {

/**
 * PermHandler interface mock
 */
class PermHandlerMock : public PermHandlerItf {
public:
    MOCK_METHOD(RetWithError<StaticString<uuid::cUUIDStrLen>>, RegisterInstance,
        (const InstanceIdent& instanceIdent, const Array<FunctionalServicePermissions>& instancePermissions),
        (override));
    MOCK_METHOD(Error, UnregisterInstance, (const InstanceIdent& instanceIdent), (override));
    MOCK_METHOD(Error, GetPermissions,
        (const String& secretUUID, const String& funcServerID, InstanceIdent& instanceIdent,
            Array<PermKeyValue>& servicePermissions),
        (override));
};

} // namespace permhandler
} // namespace iam
} // namespace aos

#endif
