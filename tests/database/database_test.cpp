/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "database/database.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Utils
 **********************************************************************************************************************/

template <typename T1, typename T2>
void FillArray(const std::initializer_list<T1>& src, aos::Array<T2>& dst)
{
    for (const auto& val : src) {
        ASSERT_TRUE(dst.PushBack(val).IsNone());
    }
}

static aos::CPUInfo CreateCPUInfo()
{
    aos::CPUInfo cpuInfo;

    cpuInfo.mModelName  = "11th Gen Intel(R) Core(TM) i7-1185G7 @ 3.00GHz";
    cpuInfo.mNumCores   = 4;
    cpuInfo.mNumThreads = 4;
    cpuInfo.mArch       = "GenuineIntel";
    cpuInfo.mArchFamily = "6";

    return cpuInfo;
}

static aos::PartitionInfo CreatePartitionInfo(const char* name, const std::initializer_list<const char*> types)
{
    aos::PartitionInfo partitionInfo;

    partitionInfo.mName = name;
    FillArray(types, partitionInfo.mTypes);
    partitionInfo.mTotalSize = 16169908;
    partitionInfo.mPath      = "/sys/kernel/tracing";
    partitionInfo.mUsedSize  = 64156;

    return partitionInfo;
}

static aos::NodeAttribute CreateAttribute(const char* name, const char* value)
{
    aos::NodeAttribute attribute;

    attribute.mName  = name;
    attribute.mValue = value;

    return attribute;
}

static aos::NodeInfo DefaultNodeInfo(const char* id = "node0")
{
    aos::NodeInfo nodeInfo;

    nodeInfo.mNodeID   = id;
    nodeInfo.mNodeType = "main";
    nodeInfo.mName     = "node0";
    nodeInfo.mStatus   = aos::NodeStatusEnum::eProvisioned;
    nodeInfo.mOSType   = "linux";
    FillArray({CreateCPUInfo(), CreateCPUInfo(), CreateCPUInfo()}, nodeInfo.mCPUs);
    FillArray({CreatePartitionInfo("trace", {"tracefs"}), CreatePartitionInfo("tmp", {})}, nodeInfo.mPartitions);
    FillArray({CreateAttribute("attr1", "val1"), CreateAttribute("attr2", "val2")}, nodeInfo.mAttrs);
    nodeInfo.mMaxDMIPS = 429138;
    nodeInfo.mTotalRAM = 32 * 1024;

    return nodeInfo;
}

static void CreateSessionTable(Poco::Data::Session& session)
{
    session << "CREATE TABLE IF NOT EXISTS certificates ("
               "type TEXT NOT NULL,"
               "issuer BLOB NOT NULL,"
               "serial BLOB NOT NULL,"
               "certURL TEXT,"
               "keyURL TEXT,"
               "notAfter TIMESTAMP,"
               "PRIMARY KEY (issuer, serial));",
        Poco::Data::Keywords::now;
}

void CreateVersionTable(Poco::Data::Session& session, int version)
{
    session << "CREATE TABLE IF NOT EXISTS SchemaVersion (version INTEGER);", Poco::Data::Keywords::now;
    session << "INSERT INTO SchemaVersion (version) VALUES(?);", Poco::Data::Keywords::use(version),
        Poco::Data::Keywords::now;
}

static void AddCertificate(Poco::Data::Session& session, const std::string& type, const std::vector<uint8_t>& issuer,
    const std::vector<uint8_t>& serial, const std::string& certURL, const std::string& keyURL)
{
    using Poco::Data::Keywords::bind;
    session << "INSERT INTO certificates (type, issuer, serial, certURL, keyURL, notAfter) "
               "VALUES (?, ?, ?, ?, ?, ?)",
        bind(type), bind(Poco::Data::BLOB {issuer.data(), issuer.size()}),
        bind(Poco::Data::BLOB {serial.data(), serial.size()}), bind(certURL), bind(keyURL), bind(uint64_t(1000)),
        Poco::Data::Keywords::now;
}

std::string GetMigrationSourceDir()
{
    std::filesystem::path curFilePath(__FILE__);
    std::filesystem::path migrationSourceDir = curFilePath.parent_path() / "../.." / "src/database/migration/";

    return std::filesystem::canonical(migrationSourceDir).string();
}

template <typename T>
const aos::Array<T> ToArray(std::vector<T>& src)
{
    return aos::Array<T>(src.data(), src.size());
}

class TestDatabase : public Database {
public:
    void SetVersion(int version) { mVersion = version; }

private:
    int GetVersion() const override { return mVersion; }

    int mVersion = 1;
};

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class DatabaseTest : public Test {
protected:
    void SetUp() override
    {
        // Preliminary clean up.
        TearDown();

        namespace fs = std::filesystem;

        auto migrationSrc = GetMigrationSourceDir();
        auto migrationDst = fs::current_path() / cMigrationPath;
        auto workingDir   = fs::current_path() / cWorkingDir;

        mMigrationConfig.mMigrationPath       = cMigrationPath;
        mMigrationConfig.mMergedMigrationPath = cMergedMigrationPath;

        fs::create_directories(cMigrationPath);

        mCMPinPath = workingDir / "cm.path.txt";
        mSMPinPath = (workingDir / "sm.path.txt");

        mMigrationConfig.mPathToPin[mCMPinPath] = "ca3b303c3c3f572e87c97a753cc7f5";
        mMigrationConfig.mPathToPin[mSMPinPath] = "ca3b303c3c3f572e87c97a753cc7f6";

        fs::copy(migrationSrc, migrationDst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }

    void TearDown() override { std::filesystem::remove_all(cWorkingDir); }

    const aos::Array<uint8_t> StringToDN(const char* str)
    {
        return aos::Array<uint8_t>(reinterpret_cast<const uint8_t*>(str), strlen(str) + 1);
    }

protected:
    static constexpr auto cWorkingDir          = "database";
    static constexpr auto cMigrationPath       = "database/migration";
    static constexpr auto cMergedMigrationPath = "database/merged-migration";

    std::string mCMPinPath, mSMPinPath;

    MigrationConfig mMigrationConfig;
    TestDatabase    mDB;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(DatabaseTest, AddCertInfo)
{
    aos::iam::certhandler::CertInfo certInfo;

    certInfo.mIssuer   = StringToDN("issuer");
    certInfo.mSerial   = StringToDN("serial");
    certInfo.mCertURL  = "certURL";
    certInfo.mKeyURL   = "keyURL";
    certInfo.mNotAfter = aos::Time::Now();

    EXPECT_EQ(mDB.Init(cWorkingDir, mMigrationConfig), aos::ErrorEnum::eNone);

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo), aos::ErrorEnum::eNone);
    EXPECT_EQ(mDB.AddCertInfo("type", certInfo), aos::ErrorEnum::eFailed);

    certInfo.mIssuer  = StringToDN("issuer2");
    certInfo.mSerial  = StringToDN("serial2");
    certInfo.mCertURL = "certURL2";
    certInfo.mKeyURL  = "keyURL2";

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo), aos::ErrorEnum::eNone);
}

TEST_F(DatabaseTest, RemoveCertInfo)
{
    EXPECT_EQ(mDB.Init(cWorkingDir, mMigrationConfig), aos::ErrorEnum::eNone);

    aos::iam::certhandler::CertInfo certInfo;

    certInfo.mIssuer  = StringToDN("issuer");
    certInfo.mSerial  = StringToDN("serial");
    certInfo.mCertURL = "certURL";
    certInfo.mKeyURL  = "keyURL";

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo), aos::ErrorEnum::eNone);

    EXPECT_EQ(mDB.RemoveCertInfo("type", "certURL"), aos::ErrorEnum::eNone);
    EXPECT_EQ(mDB.RemoveCertInfo("type", "certURL"), aos::ErrorEnum::eNone);
}

TEST_F(DatabaseTest, RemoveAllCertsInfo)
{
    EXPECT_EQ(mDB.Init(cWorkingDir, mMigrationConfig), aos::ErrorEnum::eNone);

    aos::iam::certhandler::CertInfo certInfo;

    certInfo.mIssuer  = StringToDN("issuer");
    certInfo.mSerial  = StringToDN("serial");
    certInfo.mCertURL = "certURL";
    certInfo.mKeyURL  = "keyURL";

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo), aos::ErrorEnum::eNone);

    certInfo.mIssuer  = StringToDN("issuer2");
    certInfo.mSerial  = StringToDN("serial2");
    certInfo.mCertURL = "certURL2";
    certInfo.mKeyURL  = "keyURL2";

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo), aos::ErrorEnum::eNone);

    EXPECT_EQ(mDB.RemoveAllCertsInfo("type"), aos::ErrorEnum::eNone);
    EXPECT_EQ(mDB.RemoveAllCertsInfo("type"), aos::ErrorEnum::eNone);
}

TEST_F(DatabaseTest, GetCertInfo)
{
    EXPECT_EQ(mDB.Init(cWorkingDir, mMigrationConfig), aos::ErrorEnum::eNone);

    aos::iam::certhandler::CertInfo certInfo {};

    EXPECT_EQ(mDB.GetCertInfo(certInfo.mIssuer, certInfo.mSerial, certInfo), aos::ErrorEnum::eNotFound);

    certInfo.mIssuer   = StringToDN("issuer");
    certInfo.mSerial   = StringToDN("serial");
    certInfo.mCertURL  = "certURL";
    certInfo.mKeyURL   = "keyURL";
    certInfo.mNotAfter = aos::Time::Now();

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo), aos::ErrorEnum::eNone);

    aos::iam::certhandler::CertInfo certInfo2;

    certInfo2.mIssuer   = StringToDN("issuer2");
    certInfo2.mSerial   = StringToDN("serial2");
    certInfo2.mCertURL  = "certURL2";
    certInfo2.mKeyURL   = "keyURL2";
    certInfo2.mNotAfter = aos::Time::Now();

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo2), aos::ErrorEnum::eNone);

    aos::iam::certhandler::CertInfo certInfoStored {};

    EXPECT_EQ(mDB.GetCertInfo(certInfo.mIssuer, certInfo.mSerial, certInfoStored), aos::ErrorEnum::eNone);
    EXPECT_EQ(certInfo, certInfoStored);

    EXPECT_EQ(mDB.GetCertInfo(certInfo2.mIssuer, certInfo2.mSerial, certInfoStored), aos::ErrorEnum::eNone);
    EXPECT_EQ(certInfo2, certInfoStored);
}

TEST_F(DatabaseTest, GetCertsInfo)
{
    EXPECT_EQ(mDB.Init(cWorkingDir, mMigrationConfig), aos::ErrorEnum::eNone);

    aos::StaticArray<aos::iam::certhandler::CertInfo, 2> certsInfo;

    EXPECT_EQ(mDB.GetCertsInfo("type", certsInfo), aos::ErrorEnum::eNone);
    EXPECT_TRUE(certsInfo.IsEmpty());

    aos::iam::certhandler::CertInfo certInfo;

    certInfo.mIssuer   = StringToDN("issuer");
    certInfo.mSerial   = StringToDN("serial");
    certInfo.mCertURL  = "certURL";
    certInfo.mKeyURL   = "keyURL";
    certInfo.mNotAfter = aos::Time::Now();

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo), aos::ErrorEnum::eNone);

    aos::iam::certhandler::CertInfo certInfo2;

    certInfo2.mIssuer   = StringToDN("issuer2");
    certInfo2.mSerial   = StringToDN("serial2");
    certInfo2.mCertURL  = "certURL2";
    certInfo2.mKeyURL   = "keyURL2";
    certInfo2.mNotAfter = aos::Time::Now();

    EXPECT_EQ(mDB.AddCertInfo("type", certInfo2), aos::ErrorEnum::eNone);

    EXPECT_EQ(mDB.GetCertsInfo("type", certsInfo), aos::ErrorEnum::eNone);

    EXPECT_EQ(certsInfo.Size(), 2);
    EXPECT_TRUE(certsInfo[0] == certInfo || certsInfo[1] == certInfo);
    EXPECT_TRUE(certsInfo[0] == certInfo2 || certsInfo[1] == certInfo2);

    aos::StaticArray<aos::iam::certhandler::CertInfo, 1> certsInfoNotEnoughMemory;
    EXPECT_EQ(mDB.GetCertsInfo("type", certsInfoNotEnoughMemory), aos::ErrorEnum::eNoMemory);

    ASSERT_EQ(certsInfoNotEnoughMemory.Size(), 1);
    EXPECT_TRUE(certsInfoNotEnoughMemory[0] == certInfo || certsInfoNotEnoughMemory[0] == certInfo2);
}

TEST_F(DatabaseTest, GetNodeInfo)
{
    const auto& nodeInfo = DefaultNodeInfo();

    ASSERT_TRUE(mDB.Init(cWorkingDir, mMigrationConfig).IsNone());

    ASSERT_TRUE(mDB.SetNodeInfo(nodeInfo).IsNone());

    aos::NodeInfo resultNodeInfo;
    ASSERT_TRUE(mDB.GetNodeInfo(nodeInfo.mNodeID, resultNodeInfo).IsNone());
    ASSERT_EQ(resultNodeInfo, nodeInfo);
}

TEST_F(DatabaseTest, GetAllNodeIds)
{
    const auto& node0 = DefaultNodeInfo("node0");
    const auto& node1 = DefaultNodeInfo("node1");
    const auto& node2 = DefaultNodeInfo("node2");

    ASSERT_TRUE(mDB.Init(cWorkingDir, mMigrationConfig).IsNone());

    ASSERT_TRUE(mDB.SetNodeInfo(node0).IsNone());
    ASSERT_TRUE(mDB.SetNodeInfo(node1).IsNone());
    ASSERT_TRUE(mDB.SetNodeInfo(node2).IsNone());

    aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, aos::cMaxNumNodes> expectedNodeIds, resultNodeIds;
    FillArray({node0.mNodeID, node1.mNodeID, node2.mNodeID}, expectedNodeIds);

    ASSERT_TRUE(mDB.GetAllNodeIds(resultNodeIds).IsNone());
    ASSERT_EQ(expectedNodeIds, resultNodeIds);
}

TEST_F(DatabaseTest, GetAllNodeIdsNotEnoughMemory)
{
    const auto& node0 = DefaultNodeInfo("node0");
    const auto& node1 = DefaultNodeInfo("node1");
    const auto& node2 = DefaultNodeInfo("node2");

    ASSERT_TRUE(mDB.Init(cWorkingDir, mMigrationConfig).IsNone());

    ASSERT_TRUE(mDB.SetNodeInfo(node0).IsNone());
    ASSERT_TRUE(mDB.SetNodeInfo(node1).IsNone());
    ASSERT_TRUE(mDB.SetNodeInfo(node2).IsNone());

    aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, 2> resultNodeIds;

    ASSERT_TRUE(mDB.GetAllNodeIds(resultNodeIds).Is(aos::ErrorEnum::eNoMemory));
}

TEST_F(DatabaseTest, RemoveNodeInfo)
{
    const auto& node0 = DefaultNodeInfo("node0");
    const auto& node1 = DefaultNodeInfo("node1");
    const auto& node2 = DefaultNodeInfo("node2");

    ASSERT_TRUE(mDB.Init(cWorkingDir, mMigrationConfig).IsNone());

    ASSERT_TRUE(mDB.SetNodeInfo(node0).IsNone());
    ASSERT_TRUE(mDB.SetNodeInfo(node1).IsNone());
    ASSERT_TRUE(mDB.SetNodeInfo(node2).IsNone());

    ASSERT_TRUE(mDB.RemoveNodeInfo(node1.mNodeID).IsNone());

    aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, aos::cMaxNumNodes> expectedNodeIds, resultNodeIds;
    FillArray({node0.mNodeID, node2.mNodeID}, expectedNodeIds);

    ASSERT_TRUE(mDB.GetAllNodeIds(resultNodeIds).IsNone());
    ASSERT_EQ(expectedNodeIds, resultNodeIds);
}

TEST_F(DatabaseTest, MigrateVer0To1)
{
    // Create Version 0 db
    std::vector<unsigned char> cCM = {0x1};
    std::vector<unsigned char> cSM = {0x2};

    constexpr auto cCMVer0URL = "pkcs11:token=aoscore;object=sm;id=%2C%38%6B%2F%64%1D%6A%5E%92%2E%74%55%51%5D%93%4F?"
                                "module-path=/usr/lib/softhsm/libsofthsm2.so&pin-value=ca3b303c3c3f572e87c97a753cc7f5";
    constexpr auto cSMVer0URL = "pkcs11:token=aoscore;object=cm;id=%2A%AD%9F%7E%2A%33%15%1F%22%39%F1%57%F4%E8%CF%3A?"
                                "module-path=/usr/lib/softhsm/libsofthsm2.so&pin-value=ca3b303c3c3f572e87c97a753cc7f6";

    auto cDbPath = std::filesystem::path(cWorkingDir) / "iamanager.db";
    auto session = std::make_unique<Poco::Data::Session>("SQLite", cDbPath.c_str());

    CreateSessionTable(*session);
    CreateVersionTable(*session, 0);

    AddCertificate(*session, "sm", cSM, cSM, cSMVer0URL, cSMVer0URL);
    AddCertificate(*session, "cm", cCM, cCM, cCMVer0URL, cCMVer0URL);

    session.reset();

    // Migrate to Version1
    mDB.SetVersion(1);
    ASSERT_TRUE(mDB.Init(cWorkingDir, mMigrationConfig).IsNone());

    // Check certificates
    const std::string cCMVer1URL = "pkcs11:token=aoscore;object=sm;id=%2C%38%6B%2F%64%1D%6A%5E%92%2E%74%55%51%5D%93%4F?"
                                   "module-path=/usr/lib/softhsm/libsofthsm2.so&pin-source="
        + mCMPinPath;
    const std::string cSMVer1URL = "pkcs11:token=aoscore;object=cm;id=%2A%AD%9F%7E%2A%33%15%1F%22%39%F1%57%F4%E8%CF%3A?"
                                   "module-path=/usr/lib/softhsm/libsofthsm2.so&pin-source="
        + mSMPinPath;

    aos::iam::certhandler::CertInfo certInfo {};

    ASSERT_TRUE(mDB.GetCertInfo(ToArray(cCM), ToArray(cCM), certInfo).IsNone());
    EXPECT_EQ(certInfo.mCertURL.CStr(), cCMVer1URL);
    EXPECT_EQ(certInfo.mKeyURL.CStr(), cCMVer1URL);

    ASSERT_TRUE(mDB.GetCertInfo(ToArray(cSM), ToArray(cSM), certInfo).IsNone());
    EXPECT_EQ(certInfo.mCertURL.CStr(), cSMVer1URL);
    EXPECT_EQ(certInfo.mKeyURL.CStr(), cSMVer1URL);
}

TEST_F(DatabaseTest, MigrateVer1To0)
{
    // Create Version 0 db
    std::vector<unsigned char> cCM = {0x1};
    std::vector<unsigned char> cSM = {0x2};

    const std::string cCMVer1URL = "pkcs11:token=aoscore;object=sm;id=%2C%38%6B%2F%64%1D%6A%5E%92%2E%74%55%51%5D%93%4F?"
                                   "module-path=/usr/lib/softhsm/libsofthsm2.so&pin-source="
        + mCMPinPath;
    const std::string cSMVer1URL = "pkcs11:token=aoscore;object=cm;id=%2A%AD%9F%7E%2A%33%15%1F%22%39%F1%57%F4%E8%CF%3A?"
                                   "module-path=/usr/lib/softhsm/libsofthsm2.so&pin-source="
        + mSMPinPath;

    auto cDbPath = std::filesystem::path(cWorkingDir) / "iamanager.db";
    auto session = std::make_unique<Poco::Data::Session>("SQLite", cDbPath.c_str());

    CreateSessionTable(*session);
    CreateVersionTable(*session, 1);

    AddCertificate(*session, "sm", cSM, cSM, cSMVer1URL, cSMVer1URL);
    AddCertificate(*session, "cm", cCM, cCM, cCMVer1URL, cCMVer1URL);

    session.reset();

    // Migrate to Version0
    mDB.SetVersion(0);
    ASSERT_TRUE(mDB.Init(cWorkingDir, mMigrationConfig).IsNone());

    // Check certificates
    const std::string cCMVer0URL
        = "pkcs11:token=aoscore;object=sm;id=%2C%38%6B%2F%64%1D%6A%5E%92%2E%74%55%51%5D%93%4F?"
          "module-path=/usr/lib/softhsm/libsofthsm2.so&pin-value=ca3b303c3c3f572e87c97a753cc7f5";
    const std::string cSMVer0URL
        = "pkcs11:token=aoscore;object=cm;id=%2A%AD%9F%7E%2A%33%15%1F%22%39%F1%57%F4%E8%CF%3A?"
          "module-path=/usr/lib/softhsm/libsofthsm2.so&pin-value=ca3b303c3c3f572e87c97a753cc7f6";

    aos::iam::certhandler::CertInfo certInfo {};

    ASSERT_TRUE(mDB.GetCertInfo(ToArray(cCM), ToArray(cCM), certInfo).IsNone());
    EXPECT_EQ(certInfo.mCertURL.CStr(), cCMVer0URL);
    EXPECT_EQ(certInfo.mKeyURL.CStr(), cCMVer0URL);

    ASSERT_TRUE(mDB.GetCertInfo(ToArray(cSM), ToArray(cSM), certInfo).IsNone());
    EXPECT_EQ(certInfo.mCertURL.CStr(), cSMVer0URL);
    EXPECT_EQ(certInfo.mKeyURL.CStr(), cSMVer0URL);
}
