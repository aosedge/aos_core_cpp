/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/resourcemanager/resourcemanager.hpp>

using namespace testing;

namespace aos::sm::resourcemanager {

class ResourcemanagerTest : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

} // namespace aos::sm::resourcemanager
