/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VIS_SUBJECTS_OBSERVER_MOCK_HPP_
#define VIS_SUBJECTS_OBSERVER_MOCK_HPP_

#include <aos/iam/identhandler.hpp>
#include <gmock/gmock.h>
#include <memory>

/**
 * Subjects observer mock.
 */
class VISSubjectsObserverMock : public aos::iam::identhandler::SubjectsObserverItf {
public:
    MOCK_METHOD(aos::Error, SubjectsChanged, (const aos::Array<aos::StaticString<aos::cSubjectIDLen>>&), (override));
};

#endif
