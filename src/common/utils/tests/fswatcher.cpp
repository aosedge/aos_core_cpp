/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <list>
#include <mutex>
#include <queue>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <common/utils/fswatcher.hpp>
#include <common/utils/utils.hpp>

#include <core/common/tests/utils/log.hpp>

namespace aos::common::utils::test {

namespace {

const auto     cTestDir   = std::filesystem::path("fswatcher_test_dir");
const auto     cFilePath  = cTestDir / "testfile.txt";
constexpr auto cInodeMask = IN_MODIFY;

/**
 * FS event subscriber stub class.
 */
class FSEventSubscriberStub : public FSEventSubscriber {
public:
    FSEventSubscriberStub()
    {
        static size_t id = 0;
        mID              = ++id;
    }

    void OnFSEvent(const std::string& path, uint32_t mask) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "On FSEvent called" << Log::Field("path", path.c_str()) << Log::Field("mask", mask)
                  << Log::Field("id", mID);

        mEvents.push_back(path);
        mCondVar.notify_one();
    }

    Error WaitForEvent(const std::string& path, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};

        if (!mCondVar.wait_for(lock, timeout, [this, &path]() {
                auto it = std::find(mEvents.begin(), mEvents.end(), path);

                return it != mEvents.end();
            })) {
            return ErrorEnum::eTimeout;
        }

        auto it = std::find(mEvents.begin(), mEvents.end(), path);

        mEvents.erase(it);

        return ErrorEnum::eNone;
    }

private:
    size_t                   mID = {};
    std::vector<std::string> mEvents;
    std::mutex               mMutex;
    std::condition_variable  mCondVar;
};

struct TestParams {
    TestParams(const std::string& fileName, size_t subscribers = 1)
        : mFileName(fileName)
        , mSubscribers(subscribers)
    {
        mFileStream = std::ofstream(mFileName);
        if (!mFileStream.is_open()) {
            throw std::runtime_error("Failed to open test file: " + mFileName);
        }
    }

    void RemoveFile()
    {
        mFileStream.close();
        std::filesystem::remove(mFileName);
    }

    void WriteToFile(const std::string& content)
    {
        if (!mFileStream.is_open()) {
            throw std::runtime_error("File stream is not open: " + mFileName);
        }

        mFileStream << content << std::endl;
    }

    Error WaitForNotification(std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        for (auto& subscriber : mSubscribers) {
            auto err = subscriber.WaitForEvent(mFileName, timeout);
            if (!err.IsNone()) {
                return err;
            }
        }
        return ErrorEnum::eNone;
    }

    std::string                      mFileName;
    std::ofstream                    mFileStream;
    std::list<FSEventSubscriberStub> mSubscribers;
};

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class FSWatcherTest : public ::testing::Test {
    void SetUp() override
    {
        aos::tests::utils::InitLog();
        std::filesystem::create_directory(cTestDir);
    }

    void TearDown() override { std::filesystem::remove_all(cTestDir); }

protected:
    FSWatcher mFSWatcher {Time::cMilliseconds * 100, 3, cInodeMask};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FSWatcherTest, StopStart)
{
    ASSERT_TRUE(mFSWatcher.Init().IsNone());

    ASSERT_TRUE(mFSWatcher.Stop().Is(ErrorEnum::eWrongState));

    ASSERT_TRUE(mFSWatcher.Start().IsNone());

    ASSERT_TRUE(mFSWatcher.Start().Is(ErrorEnum::eWrongState));

    ASSERT_TRUE(mFSWatcher.Stop().IsNone());
}

TEST_F(FSWatcherTest, StartFailsIfObjectNotInitialized)
{
    ASSERT_TRUE(mFSWatcher.Start().Is(ErrorEnum::eFailed));

    ASSERT_TRUE(mFSWatcher.Stop().Is(ErrorEnum::eWrongState));
}

TEST_F(FSWatcherTest, WatchMultipleFiles)
{
    ASSERT_TRUE(mFSWatcher.Init().IsNone());

    ASSERT_TRUE(mFSWatcher.Start().IsNone());

    TestParams params[] = {
        TestParams((cTestDir / "file1.txt").string(), 3),
        TestParams((cTestDir / "file2.txt").string(), 3),
        TestParams((cTestDir / "file3.txt").string(), 3),
    };

    for (auto& param : params) {
        for (auto& subscriber : param.mSubscribers) {
            ASSERT_TRUE(mFSWatcher.Subscribe(param.mFileName, subscriber).IsNone());
        }
    }

    for (auto& param : params) {
        param.WriteToFile("Initial content");
    }

    for (auto& param : params) {
        auto err = param.WaitForNotification();
        EXPECT_TRUE(err.IsNone());
    }

    for (auto& param : params) {
        if (param.mSubscribers.empty()) {
            continue;
        }

        auto it = std::prev(param.mSubscribers.end());

        EXPECT_TRUE(mFSWatcher.Unsubscribe(param.mFileName, *it).IsNone());

        param.mSubscribers.erase(it);
    }

    for (auto& param : params) {
        param.WriteToFile("Updated content");
    }

    for (auto& param : params) {
        auto err = param.WaitForNotification();
        EXPECT_TRUE(err.IsNone());

        for (auto& subscriber : param.mSubscribers) {
            EXPECT_TRUE(mFSWatcher.Unsubscribe(param.mFileName, subscriber).IsNone());
        }
    }

    ASSERT_TRUE(mFSWatcher.Stop().IsNone());
}

} // namespace aos::common::utils::test
