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
 * Static
 **********************************************************************************************************************/

class DatabaseTest : public Test {
protected:
    void TearDown() override { std::remove(mFileName.c_str()); }

    const aos::Array<uint8_t> StringToDN(const char* str)
    {
        return aos::Array<uint8_t>(reinterpret_cast<const uint8_t*>(str), strlen(str) + 1);
    }

protected:
    std::string mFileName = "test.db";
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

    EXPECT_EQ(mDB.Init(mFileName), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mDB.Init(mFileName), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mDB.Init(mFileName), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mDB.Init(mFileName), aos::ErrorEnum::eNone);

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
    EXPECT_EQ(mDB.Init(mFileName), aos::ErrorEnum::eNone);

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
