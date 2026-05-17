/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <csignal>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <common/tests/mocks/processspawnermock.hpp>
#include <sm/networkmanager/dnsserver.hpp>

using namespace aos;
using namespace aos::common::process;
using namespace aos::sm::networkmanager;
using namespace testing;

namespace {

constexpr auto cNetworkID = "net1";

constexpr Poco::Process::PID cFakePID = 12345;

} // namespace

class DNSServerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        const auto* testInfo = ::testing::UnitTest::GetInstance()->current_test_info();

        mTempDir = std::filesystem::temp_directory_path()
            / ("aos-dnsserver-" + std::to_string(::getpid()) + "-" + testInfo->name());

        std::filesystem::remove_all(mTempDir);
        std::filesystem::create_directories(mTempDir);
    }

    void TearDown() override { std::filesystem::remove_all(mTempDir); }

    void InitInstance()
    {
        EXPECT_CALL(mSpawner, Signal(cFakePID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

        ASSERT_TRUE(mInstance.Init(cNetworkID, mTempDir.string(), mSpawner, cFakePID).IsNone());

        ::testing::Mock::VerifyAndClearExpectations(&mSpawner);
    }

    std::string ReadHosts() const
    {
        std::ifstream file(mTempDir / "addnhosts");
        return std::string(std::istreambuf_iterator<char>(file), {});
    }

    DNSAliasesParams MakeParams(const char* ip, std::initializer_list<const char*> aliases) const
    {
        DNSAliasesParams params;

        params.mIP = ip;

        for (const auto* alias : aliases) {
            params.mAliases.PushBack(alias);
        }

        return params;
    }

    std::filesystem::path          mTempDir;
    StrictMock<MockProcessSpawner> mSpawner;
    DNSServer                      mInstance;
};

TEST_F(DNSServerTest, InitClearsExistingAddnhosts)
{
    std::ofstream(mTempDir / "addnhosts") << "10.0.0.99\tstale\tstale.net1\n";

    InitInstance();

    EXPECT_TRUE(ReadHosts().empty());
}

TEST_F(DNSServerTest, AddHostWritesFileAndSignalsReload)
{
    InitInstance();

    EXPECT_CALL(mSpawner, Signal(cFakePID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mInstance.AddHost("inst1", MakeParams("10.0.0.5", {"myapp"})).IsNone());

    EXPECT_EQ(ReadHosts(), "10.0.0.5\tmyapp\tmyapp.net1\n");
}

TEST_F(DNSServerTest, TwoHostsAppearOnSeparateLines)
{
    InitInstance();

    EXPECT_CALL(mSpawner, Signal(cFakePID, SIGHUP)).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mInstance.AddHost("inst1", MakeParams("10.0.0.5", {"app1"})).IsNone());
    ASSERT_TRUE(mInstance.AddHost("inst2", MakeParams("10.0.0.6", {"app2"})).IsNone());

    // std::map sorts keys, so inst1's line precedes inst2's.
    EXPECT_EQ(ReadHosts(), "10.0.0.5\tapp1\tapp1.net1\n10.0.0.6\tapp2\tapp2.net1\n");
}

TEST_F(DNSServerTest, RemoveHostUpdatesFileAndSignalsReload)
{
    InitInstance();

    EXPECT_CALL(mSpawner, Signal(cFakePID, SIGHUP)).Times(3).WillRepeatedly(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mInstance.AddHost("inst1", MakeParams("10.0.0.5", {"app1"})).IsNone());
    ASSERT_TRUE(mInstance.AddHost("inst2", MakeParams("10.0.0.6", {"app2"})).IsNone());

    ASSERT_TRUE(mInstance.RemoveHost("inst1").IsNone());

    EXPECT_EQ(ReadHosts(), "10.0.0.6\tapp2\tapp2.net1\n");
}

TEST_F(DNSServerTest, RemoveAbsentHostIsNoOp)
{
    InitInstance();

    // StrictMock asserts there is zero further spawner interaction.
    EXPECT_TRUE(mInstance.RemoveHost("nope").IsNone());
    EXPECT_TRUE(ReadHosts().empty());
}

TEST_F(DNSServerTest, ReAddSameInstanceReplacesEntry)
{
    InitInstance();

    EXPECT_CALL(mSpawner, Signal(cFakePID, SIGHUP)).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mInstance.AddHost("inst1", MakeParams("10.0.0.5", {"old"})).IsNone());
    ASSERT_TRUE(mInstance.AddHost("inst1", MakeParams("10.0.0.5", {"renamed"})).IsNone());

    EXPECT_EQ(ReadHosts(), "10.0.0.5\trenamed\trenamed.net1\n");
}

TEST_F(DNSServerTest, EachAliasResolvesBareAndFullyQualified)
{
    InitInstance();

    EXPECT_CALL(mSpawner, Signal(cFakePID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mInstance.AddHost("inst1", MakeParams("10.0.0.5", {"a1", "a2"})).IsNone());

    EXPECT_EQ(ReadHosts(), "10.0.0.5\ta1\ta1.net1\ta2\ta2.net1\n");
}
