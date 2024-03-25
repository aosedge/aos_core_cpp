/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "logger/logger.hpp"
#include "utils/exception.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class ExceptionTest : public Test {
protected:
    void SetUp() override
    {
        Logger logger;

        logger.Init();
    }

    void FunctionWithException()
    {
        mFileName = __FILENAME__;
        mLineNum  = __LINE__ + 1;
        AOS_ERROR_THROW("oops", aos::ErrorEnum::eFailed);
    }

    std::string GetFileAndLine() const { return "(" + mFileName + ":" + std::to_string(mLineNum) + ")"; }

private:
    std::string mFileName;
    int         mLineNum = 0;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ExceptionTest, ThrowAosException)
{
    try {
        FunctionWithException();
        FAIL() << "AosException expected";
    } catch (const AosException& e) {
        EXPECT_EQ(e.message(), std::string("oops: failed ") + GetFileAndLine());
    } catch (...) {
        FAIL() << "AosException expected";
    }
}
