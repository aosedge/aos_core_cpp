/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UNITLISTENER_MOCK_HPP_
#define UNITLISTENER_MOCK_HPP_

#include <gmock/gmock.h>

#include <sm/runner/runner.hpp>

namespace aos::sm::runner {

class RunStatusReceiverMock : public RunStatusReceiverItf {
public:
    MOCK_METHOD(Error, UpdateRunStatus, (const Array<RunStatus>&), (override));
};

} // namespace aos::sm::runner

#endif
