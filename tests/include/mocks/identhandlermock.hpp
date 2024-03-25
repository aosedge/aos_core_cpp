/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IDENTHANDLER_ITF_MOCK_HPP_
#define IDENTHANDLER_ITF_MOCK_HPP_

#include <aos/iam/identhandler.hpp>
#include <gmock/gmock.h>

namespace aos {
namespace iam {
namespace identhandler {

/**
 * IdentHandler interface mock
 */
class IdentHandlerMock : public IdentHandlerItf {
public:
    MOCK_METHOD(RetWithError<StaticString<cSystemIDLen>>, GetSystemID, (), (override));
    MOCK_METHOD(RetWithError<StaticString<cUnitModelLen>>, GetUnitModel, (), (override));
    MOCK_METHOD(Error, GetSubjects, (Array<StaticString<cSubjectIDLen>> & subjects), (override));
};

} // namespace identhandler
} // namespace iam
} // namespace aos

#endif
