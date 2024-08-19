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

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class DatabaseTest : public Test {
protected:
    void TearDown() override { std::remove(mFileName.c_str()); }

    const aos::Array<uint8_t> StringToDN(const char* str)
    {
        return aos::Array<uint8_t>(reinterpret_cast<const uint8_t*>(str), strlen(str) + 1);
    }

protected:
    std::string mFileName      = "database/test/test.db";
    std::string mMigrationPath = "database/test/migration";
    Database    mDB;
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

    EXPECT_EQ(mDB.Init(mFileName, mMigrationPath), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mDB.Init(mFileName, mMigrationPath), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mDB.Init(mFileName, mMigrationPath), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mDB.Init(mFileName, mMigrationPath), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mDB.Init(mFileName, mMigrationPath), aos::ErrorEnum::eNone);

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
}

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(DatabaseTest, GetNodeInfo)
{
    const auto& nodeInfo = DefaultNodeInfo();

    ASSERT_TRUE(mDB.Init(mFileName, mMigrationPath).IsNone());

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

    ASSERT_TRUE(mDB.Init(mFileName, mMigrationPath).IsNone());

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

    ASSERT_TRUE(mDB.Init(mFileName, mMigrationPath).IsNone());

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

    ASSERT_TRUE(mDB.Init(mFileName, mMigrationPath).IsNone());

    ASSERT_TRUE(mDB.SetNodeInfo(node0).IsNone());
    ASSERT_TRUE(mDB.SetNodeInfo(node1).IsNone());
    ASSERT_TRUE(mDB.SetNodeInfo(node2).IsNone());

    ASSERT_TRUE(mDB.RemoveNodeInfo(node1.mNodeID).IsNone());

    aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, aos::cMaxNumNodes> expectedNodeIds, resultNodeIds;
    FillArray({node0.mNodeID, node2.mNodeID}, expectedNodeIds);

    ASSERT_TRUE(mDB.GetAllNodeIds(resultNodeIds).IsNone());
    ASSERT_EQ(expectedNodeIds, resultNodeIds);
}
