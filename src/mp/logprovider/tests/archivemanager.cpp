/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/InflatingStream.h>
#include <Poco/StreamCopier.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include <mp/logprovider/archivemanager.hpp>

using namespace testing;

namespace aos::mp::logprovider {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cLogID       = "test_log";
const auto     cStatusOk    = cloudprotocol::LogStatus(cloudprotocol::LogStatusEnum::eOk);
const auto     cStatusEmpty = cloudprotocol::LogStatus(cloudprotocol::LogStatusEnum::eEmpty);

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::string DecompressGZIP(const std::string& compressedData)
{
    std::istringstream         compressedStream(compressedData);
    Poco::InflatingInputStream inflater(compressedStream, Poco::InflatingStreamBuf::STREAM_GZIP);

    std::ostringstream decompressedStream;
    Poco::StreamCopier::copyStream(inflater, decompressedStream);

    return decompressedStream.str();
}

std::string GenerateExpectedLog(const std::vector<servicemanager::v4::LogData>& logs)
{
    std::ostringstream oss;

    for (const auto& log : logs) {
        oss << log.data();
    }

    return oss.str();
}

std::vector<servicemanager::v4::LogData> CreateLogChunks(
    const std::string& logID, const std::vector<std::string>& chunks)
{
    std::vector<servicemanager::v4::LogData> logChunks;

    for (size_t i = 0; i < chunks.size(); ++i) {
        servicemanager::v4::LogData logChunk;

        logChunk.set_log_id(logID);
        logChunk.set_part_count(chunks.size());
        logChunk.set_part(i + 1);
        logChunk.set_status(cStatusOk.ToString().CStr());
        logChunk.set_data(chunks[i]);

        logChunks.push_back(std::move(logChunk));
    }

    servicemanager::v4::LogData lastLogChunk;
    lastLogChunk.set_log_id(logID);
    lastLogChunk.set_status(cStatusEmpty.ToString().CStr());

    logChunks.push_back(std::move(lastLogChunk));

    return logChunks;
}

/**
 * Log observer stub.
 */
class LogObserverStub : public aos::sm::logprovider::LogObserverItf {
public:
    Error OnLogReceived(const cloudprotocol::PushLog& log) override
    {
        std::unique_lock<std::mutex> lock(mMutex);

        mLogQueue.push(log);
        mCondVar.notify_one();

        return ErrorEnum::eNone;
    }

    Error WaitLogReceived(
        cloudprotocol::PushLog& log, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
    {
        std::unique_lock<std::mutex> lock(mMutex);

        mCondVar.wait_for(lock, timeout, [this] { return !mLogQueue.empty(); });

        if (mLogQueue.empty()) {
            return ErrorEnum::eTimeout;
        }

        log = mLogQueue.front();
        mLogQueue.pop();

        return ErrorEnum::eNone;
    }

private:
    std::mutex                         mMutex;
    std::condition_variable            mCondVar;
    std::queue<cloudprotocol::PushLog> mLogQueue;
};

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class ArchiveManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::test::InitLog();

        mConfig.mMaxPartCount = 10;
        mConfig.mMaxPartSize  = 1024;

        EXPECT_EQ(mArchiveManager.Init(mLogObserver, mConfig), ErrorEnum::eNone);
    }

    void TearDown() override { }

    aos::logprovider::Config mConfig;
    LogObserverStub          mLogObserver;
    ArchiveManager           mArchiveManager;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ArchiveManagerTest, HandleLogChunks)
{
    EXPECT_EQ(mArchiveManager.Start(), ErrorEnum::eNone);

    const auto logChunks   = CreateLogChunks(cLogID, {"test_chunk_1", "test_chunk_2"});
    const auto expectedLog = GenerateExpectedLog(logChunks);

    for (const auto& log : logChunks) {
        EXPECT_EQ(mArchiveManager.HandleLog(std::make_shared<servicemanager::v4::LogData>(log)), ErrorEnum::eNone);
    }

    auto receivedLog = std::make_shared<cloudprotocol::PushLog>();

    EXPECT_EQ(mLogObserver.WaitLogReceived(*receivedLog), ErrorEnum::eNone);
    EXPECT_STREQ(receivedLog->mLogID.CStr(), cLogID);
    EXPECT_EQ(receivedLog->mPartsCount, 1);
    EXPECT_EQ(receivedLog->mPart, 1);
    EXPECT_EQ(receivedLog->mStatus, cStatusOk);
    EXPECT_EQ(DecompressGZIP(std::string(receivedLog->mContent.begin(), receivedLog->mContent.end())), expectedLog);

    EXPECT_EQ(mArchiveManager.Stop(), ErrorEnum::eNone);
}

TEST_F(ArchiveManagerTest, HandleEmpty)
{
    EXPECT_EQ(mArchiveManager.Start(), ErrorEnum::eNone);

    auto log = std::make_shared<servicemanager::v4::LogData>();
    log->set_log_id(cLogID);
    log->set_part_count(1);
    log->set_part(1);
    log->set_status(cStatusEmpty.ToString().CStr());

    EXPECT_EQ(mArchiveManager.HandleLog(log), ErrorEnum::eNone);

    auto receivedLog = std::make_shared<cloudprotocol::PushLog>();

    EXPECT_EQ(mLogObserver.WaitLogReceived(*receivedLog), ErrorEnum::eNone);
    EXPECT_STREQ(receivedLog->mLogID.CStr(), cLogID);
    EXPECT_EQ(receivedLog->mPartsCount, 1);
    EXPECT_EQ(receivedLog->mPart, 1);
    EXPECT_EQ(receivedLog->mStatus, cStatusEmpty);

    EXPECT_EQ(mArchiveManager.Stop(), ErrorEnum::eNone);
}

} // namespace aos::mp::logprovider
