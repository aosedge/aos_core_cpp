/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/types/common.hpp>

#include <common/tests/mocks/processspawnermock.hpp>
#include <sm/networkmanager/dnsname.hpp>

using namespace aos;
using namespace aos::common::process;
using namespace aos::sm::networkmanager;
using namespace testing;

namespace {

constexpr auto cDnsmasqBinary = "/usr/sbin/dnsmasq";

constexpr Poco::Process::PID cSpawnedPID = 12345;
constexpr Poco::Process::PID cAdoptPID   = 4242;
constexpr Poco::Process::PID cStalePID   = 4242;
constexpr Poco::Process::PID cRespawnPID = 4243;
constexpr Poco::Process::PID cOrphanPID  = 55;

} // namespace

class DNSNameTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        const auto* testInfo = ::testing::UnitTest::GetInstance()->current_test_info();

        mRoot = std::filesystem::temp_directory_path()
            / ("aos-dnsname-" + std::to_string(::getpid()) + "-" + testInfo->name());

        std::filesystem::remove_all(mRoot);

        ASSERT_TRUE(mName.Init(mRoot.string(), mSpawner).IsNone());
    }

    void TearDown() override { std::filesystem::remove_all(mRoot); }

    DNSServerParams MakeParams() const
    {
        DNSServerParams params;

        params.mBridgeIP     = "10.0.0.1";
        params.mBridgeIfName = "br0";

        return params;
    }

    std::vector<std::string> ExpectedArgs(const std::filesystem::path& storageDir) const
    {
        return {
            "--keep-in-foreground",
            "--no-hosts",
            "--no-resolv",
            "--resolv-file=/etc/resolv.conf",
            "--bind-interfaces",
            "--interface=br0",
            "--listen-address=10.0.0.1",
            "--addn-hosts=" + storageDir.string() + "/addnhosts",
            "--pid-file=" + storageDir.string() + "/pidfile",
            "--conf-file=/dev/null",
        };
    }

    void WritePidFile(const std::filesystem::path& dir, Poco::Process::PID pid) const
    {
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "pidfile") << pid;
    }

    std::filesystem::path          mRoot;
    StrictMock<MockProcessSpawner> mSpawner;
    DNSName                        mName;
};

TEST_F(DNSNameTest, CreateServerFreshSpawnsDnsmasq)
{
    const auto storageDir = mRoot / "net1";

    EXPECT_CALL(mSpawner, Spawn(std::string(cDnsmasqBinary), ExpectedArgs(storageDir)))
        .WillOnce(Return(RetWithError<Poco::Process::PID> {cSpawnedPID, ErrorEnum::eNone}));
    EXPECT_CALL(mSpawner, Signal(cSpawnedPID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

    auto [handle, err] = mName.CreateServer("net1", MakeParams());

    ASSERT_TRUE(err.IsNone());
    EXPECT_NE(handle, nullptr);
    EXPECT_TRUE(std::filesystem::exists(storageDir));
}

TEST_F(DNSNameTest, CreateServerSamePointerOnRepeat)
{
    EXPECT_CALL(mSpawner, Spawn(_, _))
        .WillOnce(Return(RetWithError<Poco::Process::PID> {cSpawnedPID, ErrorEnum::eNone}));
    EXPECT_CALL(mSpawner, Signal(cSpawnedPID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

    auto [first, errFirst] = mName.CreateServer("net1", MakeParams());

    ASSERT_TRUE(errFirst.IsNone());

    auto [second, errSecond] = mName.CreateServer("net1", MakeParams());

    ASSERT_TRUE(errSecond.IsNone());
    EXPECT_EQ(first, second);
}

TEST_F(DNSNameTest, CreateServerAdoptsAlivePidFile)
{
    const auto storageDir = mRoot / "net1";

    WritePidFile(storageDir, cAdoptPID);

    const std::string cmdline = "/usr/sbin/dnsmasq --pid-file=" + storageDir.string() + "/pidfile";

    EXPECT_CALL(mSpawner, IsAlive(cAdoptPID)).WillOnce(Return(true));
    EXPECT_CALL(mSpawner, GetCmdLine(cAdoptPID))
        .WillOnce(Return(RetWithError<std::string> {cmdline, ErrorEnum::eNone}));
    EXPECT_CALL(mSpawner, Signal(cAdoptPID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

    auto [handle, err] = mName.CreateServer("net1", MakeParams());

    ASSERT_TRUE(err.IsNone());
    EXPECT_NE(handle, nullptr);
}

TEST_F(DNSNameTest, CreateServerRespawnsStalePidFile)
{
    const auto storageDir = mRoot / "net1";

    WritePidFile(storageDir, cStalePID);

    InSequence seq;

    EXPECT_CALL(mSpawner, IsAlive(cStalePID)).WillOnce(Return(false));
    EXPECT_CALL(mSpawner, Spawn(_, _))
        .WillOnce(Return(RetWithError<Poco::Process::PID> {cRespawnPID, ErrorEnum::eNone}));
    EXPECT_CALL(mSpawner, Signal(cRespawnPID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

    auto [handle, err] = mName.CreateServer("net1", MakeParams());

    ASSERT_TRUE(err.IsNone());
    EXPECT_NE(handle, nullptr);
}

TEST_F(DNSNameTest, CreateServerRespawnsWhenPidIsNotDnsmasq)
{
    const auto storageDir = mRoot / "net1";

    WritePidFile(storageDir, cAdoptPID);

    InSequence seq;

    EXPECT_CALL(mSpawner, IsAlive(cAdoptPID)).WillOnce(Return(true));
    EXPECT_CALL(mSpawner, GetCmdLine(cAdoptPID))
        .WillOnce(Return(RetWithError<std::string> {"/bin/bash", ErrorEnum::eNone}));
    EXPECT_CALL(mSpawner, Spawn(_, _))
        .WillOnce(Return(RetWithError<Poco::Process::PID> {cRespawnPID, ErrorEnum::eNone}));
    EXPECT_CALL(mSpawner, Signal(cRespawnPID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

    auto [handle, err] = mName.CreateServer("net1", MakeParams());

    ASSERT_TRUE(err.IsNone());
    EXPECT_NE(handle, nullptr);
}

TEST_F(DNSNameTest, CreateServerRollsBackWhenInitFails)
{
    const auto storageDir = mRoot / "net1";

    InSequence seq;

    EXPECT_CALL(mSpawner, Spawn(_, _))
        .WillOnce(Return(RetWithError<Poco::Process::PID> {cSpawnedPID, ErrorEnum::eNone}));
    EXPECT_CALL(mSpawner, Signal(cSpawnedPID, SIGHUP)).WillOnce(Return(Error(ErrorEnum::eNotFound, "dnsmasq died")));
    EXPECT_CALL(mSpawner, Kill(cSpawnedPID)).WillOnce(Return(ErrorEnum::eNone));

    auto [handle, err] = mName.CreateServer("net1", MakeParams());

    EXPECT_FALSE(err.IsNone());
    EXPECT_EQ(handle, nullptr);
    EXPECT_FALSE(std::filesystem::exists(storageDir));
}

TEST_F(DNSNameTest, RemoveServerKillsAndWipesDir)
{
    const auto storageDir = mRoot / "net1";

    EXPECT_CALL(mSpawner, Spawn(_, _))
        .WillOnce(Return(RetWithError<Poco::Process::PID> {cSpawnedPID, ErrorEnum::eNone}));
    EXPECT_CALL(mSpawner, Signal(cSpawnedPID, SIGHUP)).WillOnce(Return(ErrorEnum::eNone));

    auto [handle, err] = mName.CreateServer("net1", MakeParams());

    ASSERT_TRUE(err.IsNone());
    ASSERT_TRUE(std::filesystem::exists(storageDir));

    EXPECT_CALL(mSpawner, Kill(cSpawnedPID)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mName.RemoveServer("net1").IsNone());
    EXPECT_FALSE(std::filesystem::exists(storageDir));
}

TEST_F(DNSNameTest, RemoveOrphansReapsUnknownDirsOnly)
{
    const auto orphanDir = mRoot / "orphan1";
    const auto knownDir  = mRoot / "known1";

    WritePidFile(orphanDir, cOrphanPID);
    std::filesystem::create_directories(knownDir);

    EXPECT_CALL(mSpawner, Kill(cOrphanPID)).WillOnce(Return(ErrorEnum::eNone));

    StaticArray<StaticString<cIDLen>, cMaxNumOwners> known;
    ASSERT_TRUE(known.PushBack("known1").IsNone());

    EXPECT_TRUE(mName.RemoveOrphans(known).IsNone());
    EXPECT_FALSE(std::filesystem::exists(orphanDir));
    EXPECT_TRUE(std::filesystem::exists(knownDir));
}

TEST_F(DNSNameTest, RemoveServerAbsentIsNoOp)
{
    // StrictMock asserts zero spawner interaction.
    EXPECT_TRUE(mName.RemoveServer("nope").IsNone());
}
