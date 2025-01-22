/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "utils/exception.hpp"

using namespace testing;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class ExceptionTest : public Test {
protected:
    void FunctionWithException()
    {
        mFileName = __FILENAME__;
        mLineNum  = __LINE__ + 1;
        AOS_ERROR_THROW("oops", ErrorEnum::eFailed);
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
        EXPECT_TRUE(false) << "AosException expected";
    } catch (const AosException& e) {
        const auto expectedMessage = std::string("oops: failed ") + GetFileAndLine();

        EXPECT_EQ(e.what(), "Aos exception");
        EXPECT_EQ(e.message(), expectedMessage);
        EXPECT_EQ(e.displayText(), std::string("Aos exception: ") + expectedMessage);

        EXPECT_TRUE(e.GetError().Is(ErrorEnum::eFailed));
        EXPECT_STREQ(e.GetError().Message(), "oops");
    } catch (...) {
        EXPECT_TRUE(false) << "AosException expected";
    }
}

} // namespace aos::common::utils
