/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <Poco/JSON/Object.h>
#include <gmock/gmock.h>

#include <aos/test/log.hpp>

#include "fileidentifier/fileidentifier.hpp"
#include "mocks/identhandlermock.hpp"
#include "mocks/wsclientmock.hpp"

using namespace testing;

namespace aos::iam::fileidentifier {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

constexpr auto cSystemIDPath  = "systemID";
constexpr auto cUnitModelPath = "unitModel";
constexpr auto cSubjectsPath  = "subjects";
constexpr auto cSystemID      = "systemID";
constexpr auto cUnitModel     = "unitModel";
constexpr auto cSubjects      = R"(subject1
subject2
subject3)";

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class FileIdentifierTest : public testing::Test {
protected:
    void SetUp() override
    {
        aos::test::InitLog();

        if (std::ofstream f(cSystemIDPath); f) {
            f << cSystemID;
        }

        if (std::ofstream f(cUnitModelPath); f) {
            f << cUnitModel;
        }

        if (std::ofstream f(cSubjectsPath); f) {
            f << cSubjects;
        }

        Poco::JSON::Object::Ptr object = new Poco::JSON::Object();

        object->set("systemIDPath", cSystemIDPath);
        object->set("unitModelPath", cUnitModelPath);
        object->set("subjectsPath", cSubjectsPath);

        mConfig.mParams = object;
    }

    identhandler::SubjectsObserverMock mSubjectsObserverMock;
    config::Identifier                 mConfig;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FileIdentifierTest, InitFailsOnEmptyConfig)
{
    FileIdentifier identifier;

    const auto err = identifier.Init(config::Identifier {}, mSubjectsObserverMock);
    ASSERT_FALSE(err.IsNone()) << err.Message();
}

TEST_F(FileIdentifierTest, InitFailsOnSystemIDFileMissing)
{
    FileIdentifier identifier;

    FS::Remove(cSystemIDPath);

    auto err = identifier.Init(mConfig, mSubjectsObserverMock);
    ASSERT_EQ(err.Value(), ErrorEnum::eNotFound);
}

TEST_F(FileIdentifierTest, InitFailsOnUnitModelFileMissing)
{
    FileIdentifier identifier;

    FS::Remove(cUnitModelPath);

    auto err = identifier.Init(mConfig, mSubjectsObserverMock);
    ASSERT_EQ(err.Value(), ErrorEnum::eNotFound);
}

TEST_F(FileIdentifierTest, InitSucceedsOnSubjectsFileMissing)
{
    FileIdentifier identifier;

    FS::Remove(cSubjectsPath);

    auto err = identifier.Init(mConfig, mSubjectsObserverMock);
    ASSERT_EQ(err.Value(), ErrorEnum::eNone);
}

TEST_F(FileIdentifierTest, InitFailsOnSubjectsCountExceedsAppLimit)
{
    FileIdentifier identifier;

    if (std::ofstream f(cSubjectsPath); f) {
        for (size_t i = 0; i < cMaxSubjectIDSize + 1; ++i) {
            f << "subject" << i << std::endl;
        }
    }

    auto err = identifier.Init(mConfig, mSubjectsObserverMock);
    ASSERT_EQ(err.Value(), ErrorEnum::eNoMemory);
}

TEST_F(FileIdentifierTest, InitFailsOnSubjectLenExceedsAppLimit)
{
    FileIdentifier identifier;

    if (std::ofstream f(cSubjectsPath); f) {
        f << "subject" << std::string(cSubjectIDLen, 'a') << std::endl;
    }

    auto err = identifier.Init(mConfig, mSubjectsObserverMock);
    ASSERT_EQ(err.Value(), ErrorEnum::eNoMemory);
}

TEST_F(FileIdentifierTest, GetSystemID)
{
    FileIdentifier identifier;

    const auto err = identifier.Init(mConfig, mSubjectsObserverMock);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const auto [systemID, systemIDErr] = identifier.GetSystemID();
    ASSERT_TRUE(systemIDErr.IsNone()) << systemIDErr.Message();
    ASSERT_STREQ(systemID.CStr(), cSystemID);
}

TEST_F(FileIdentifierTest, GetUnitModel)
{
    FileIdentifier identifier;

    const auto err = identifier.Init(mConfig, mSubjectsObserverMock);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const auto [unitModel, unitModelErr] = identifier.GetUnitModel();
    ASSERT_TRUE(unitModelErr.IsNone()) << unitModelErr.Message();
    ASSERT_STREQ(unitModel.CStr(), cUnitModel);
}

TEST_F(FileIdentifierTest, GetSubjects)
{
    FileIdentifier identifier;

    const auto err = identifier.Init(mConfig, mSubjectsObserverMock);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    StaticArray<StaticString<cSubjectIDLen>, cMaxSubjectIDSize> subjects;

    const auto subjectsErr = identifier.GetSubjects(subjects);
    ASSERT_TRUE(subjectsErr.IsNone()) << subjectsErr.Message();

    ASSERT_EQ(subjects.Size(), 3);
    ASSERT_STREQ(subjects[0].CStr(), "subject1");
    ASSERT_STREQ(subjects[1].CStr(), "subject2");
    ASSERT_STREQ(subjects[2].CStr(), "subject3");
}

} // namespace aos::iam::fileidentifier
