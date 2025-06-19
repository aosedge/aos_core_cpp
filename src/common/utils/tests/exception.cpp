/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/utils/exception.hpp>

using namespace testing;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class ExceptionTest : public Test {
protected:
    void FunctionWithExceptionWithMessage()
    {
        mFileName = __FILENAME__;
        mLineNum  = __LINE__ + 1;
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "oops");
    }

    void FunctionWithExceptionWithoutMessage()
    {
        aos::Error err(ErrorEnum::eRuntime, "oops");
        mFileName = __FILENAME__;
        mLineNum  = __LINE__ + 1;
        AOS_ERROR_THROW(err);
    }

    std::string GetFileAndLine() const { return "(" + mFileName + ":" + std::to_string(mLineNum) + ")"; }

private:
    std::string mFileName;
    int         mLineNum = 0;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ExceptionTest, ThrowAosExceptionWithMessage)
{
    try {
        FunctionWithExceptionWithMessage();
        EXPECT_TRUE(false) << "AosException expected";
    } catch (const AosException& e) {
        const auto expectedMessage = std::string("oops: runtime error ") + GetFileAndLine();

        EXPECT_EQ(e.what(), "Aos exception");
        EXPECT_EQ(e.message(), expectedMessage);
        EXPECT_EQ(e.displayText(), std::string("Aos exception: ") + expectedMessage);

        EXPECT_TRUE(e.GetError().Is(ErrorEnum::eRuntime));
        EXPECT_STREQ(e.GetError().Message(), "oops");
    } catch (...) {
        FAIL() << "AosException expected";
    }
}

TEST_F(ExceptionTest, ThrowAosExceptionWithoutMessage)
{
    try {
        FunctionWithExceptionWithoutMessage();
        EXPECT_TRUE(false) << "AosException expected";
    } catch (const AosException& e) {
        const auto expectedMessage = std::string("oops ") + GetFileAndLine();

        EXPECT_EQ(e.what(), "Aos exception");
        EXPECT_EQ(e.message(), expectedMessage);
        EXPECT_EQ(e.displayText(), std::string("Aos exception: ") + expectedMessage);

        EXPECT_TRUE(e.GetError().Is(ErrorEnum::eRuntime));
        EXPECT_STREQ(e.GetError().Message(), "oops");
    } catch (...) {
        FAIL() << "AosException expected";
    }
}

TEST_F(ExceptionTest, ThrowStdException)
{
    try {
        throw std::runtime_error("oops");
    } catch (const std::exception& e) {
        EXPECT_STREQ(e.what(), "oops");

        auto err = ToAosError(e);

        EXPECT_TRUE(err.Is(ErrorEnum::eFailed));
        EXPECT_STREQ(err.Message(), "oops");
    } catch (...) {
        FAIL() << "std::exception expected";
    }
}

TEST_F(ExceptionTest, ThrowPocoException)
{
    try {
        throw Poco::Exception("oops");
    } catch (const Poco::Exception& e) {
        EXPECT_EQ(e.message(), "oops");

        auto err = ToAosError(e);

        EXPECT_TRUE(err.Is(ErrorEnum::eFailed));
        EXPECT_STREQ(err.Message(), e.displayText().c_str());
    } catch (...) {
        FAIL() << "Poco::Exception expected";
    }
}

} // namespace aos::common::utils
