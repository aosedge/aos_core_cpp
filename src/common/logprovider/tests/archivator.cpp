/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <numeric>
#include <vector>

#include <Poco/InflatingStream.h>
#include <Poco/StreamCopier.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <common/logprovider/archivator.hpp>

using namespace testing;

namespace aos::common::logprovider {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

constexpr auto cLogID = "TestLogID";

std::string DecompressGZIP(const std::string& compressedData)
{
    std::istringstream         compressedStream(compressedData);
    Poco::InflatingInputStream inflater(compressedStream, Poco::InflatingStreamBuf::STREAM_GZIP);

    std::ostringstream decompressedStream;
    Poco::StreamCopier::copyStream(inflater, decompressedStream);

    return decompressedStream.str();
}

/**
 * Log observer mock.
 */
class LogObserverMock : public sm::logprovider::LogObserverItf {
public:
    MOCK_METHOD(Error, OnLogReceived, (const PushLog& log), (override));
};

class ArchivatorTest : public Test {
public:
    LogObserverMock          mLogObserver;
    aos::logprovider::Config mConfig {1024, 5};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ArchivatorTest, ArchiveEmpty)
{
    const auto logMessage = std::string {};

    Archivator archivator(mLogObserver, mConfig);

    EXPECT_CALL(mLogObserver, OnLogReceived(_)).WillOnce(Invoke([](const PushLog& log) {
        EXPECT_STREQ(log.mCorrelationID.CStr(), cLogID);
        EXPECT_EQ(log.mPartsCount, 1);
        EXPECT_EQ(log.mPart, 1);
        EXPECT_EQ(log.mStatus, LogStatusEnum::eEmpty);
        EXPECT_TRUE(log.mContent.IsEmpty());

        return ErrorEnum::eNone;
    }));

    EXPECT_EQ(archivator.SendLog(cLogID), ErrorEnum::eNone);
}

TEST_F(ArchivatorTest, ArchiveChunks)
{
    const std::vector<std::string> logMessages = {
        "Test log message 1",
        "Test log message 2",
        "Test log message 3",
        "Test log message 4",
        "Test log message 5",
    };
    const auto expectedUncompressedString = std::accumulate(logMessages.begin(), logMessages.end(), std::string {});

    Archivator archivator(mLogObserver, mConfig);

    for (const auto& logMessage : logMessages) {
        EXPECT_EQ(archivator.AddLog(logMessage), ErrorEnum::eNone);
    }

    EXPECT_CALL(mLogObserver, OnLogReceived(_)).WillOnce(Invoke([&expectedUncompressedString](const PushLog& log) {
        EXPECT_STREQ(log.mCorrelationID.CStr(), cLogID);
        EXPECT_EQ(log.mPartsCount, 1);
        EXPECT_EQ(log.mPart, 1);
        EXPECT_EQ(log.mStatus, LogStatusEnum::eOK);

        auto decompressed = DecompressGZIP(std::string(log.mContent.begin(), log.mContent.end()));
        EXPECT_EQ(decompressed, expectedUncompressedString);

        return ErrorEnum::eNone;
    }));

    EXPECT_EQ(archivator.SendLog(cLogID), ErrorEnum::eNone);
}

TEST_F(ArchivatorTest, ArchiveLongChunks)
{
    const std::vector<std::string> logMessages = {
        std::string(mConfig.mMaxPartSize, 'a'),
        std::string(mConfig.mMaxPartSize, 'b'),
        std::string(mConfig.mMaxPartSize, 'c'),
        std::string(mConfig.mMaxPartSize, 'd'),
    };

    Archivator archivator(mLogObserver, mConfig);

    for (const auto& logMessage : logMessages) {
        EXPECT_EQ(archivator.AddLog(logMessage), ErrorEnum::eNone);
    }

    std::vector<PushLog> pushedLogs;

    EXPECT_CALL(mLogObserver, OnLogReceived(_)).WillRepeatedly(Invoke([&pushedLogs](const PushLog& log) {
        pushedLogs.push_back(log);

        return ErrorEnum::eNone;
    }));

    EXPECT_EQ(archivator.SendLog(cLogID), ErrorEnum::eNone);

    EXPECT_EQ(pushedLogs.size(), logMessages.size());

    for (size_t i = 0; i < pushedLogs.size(); ++i) {
        const auto& log = pushedLogs[i];

        EXPECT_STREQ(log.mCorrelationID.CStr(), cLogID);
        EXPECT_EQ(log.mPartsCount, logMessages.size());
        EXPECT_EQ(log.mPart, i + 1);
        EXPECT_EQ(log.mStatus, LogStatusEnum::eOK);

        auto decompressed = DecompressGZIP(std::string(log.mContent.begin(), log.mContent.end()));
        EXPECT_EQ(decompressed, logMessages[i]);
    }
}

} // namespace aos::common::logprovider
