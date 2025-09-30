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
#include <core/common/tests/utils/utils.hpp>

namespace aos::common::utils::test {

namespace {

const auto                     cTestDir       = std::filesystem::path("fswatcher_test_dir");
const auto                     cFilePath      = cTestDir / "testfile.txt";
const std::vector<fs::FSEvent> cWatchedEvents = {fs::FSEventEnum::eModify};
const auto                     cPollTimeout   = Time::cMilliseconds * 100;
const auto                     cNotifyTimeout = 3 * cPollTimeout;

/**
 * FS event subscriber stub class.
 */
class FSEventSubscriberStub : public fs::FSEventSubscriberItf {
public:
    FSEventSubscriberStub()
    {
        static size_t id = 0;
        mID              = ++id;
    }

    void OnFSEvent(const String& path, const Array<fs::FSEvent>& events) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "On FSEvent called" << Log::Field("path", path) << Log::Field("eventsCount", events.Size())
                  << Log::Field("id", mID);

        mEvents[path.CStr()] = {events.begin(), events.end()};
        mCondVar.notify_one();
    }

    Error WaitForEvent(const String& path, std::vector<fs::FSEvent>& events,
        std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};

        if (!mCondVar.wait_for(lock, timeout, [this, &path]() { return mEvents.count(path.CStr()) > 0; })) {
            return ErrorEnum::eTimeout;
        }

        events = mEvents[path.CStr()];

        mEvents.erase(path.CStr());

        return ErrorEnum::eNone;
    }

private:
    size_t                                          mID = {};
    std::map<std::string, std::vector<fs::FSEvent>> mEvents;
    std::mutex                                      mMutex;
    std::condition_variable                         mCondVar;
};

struct TestParams {
    TestParams(const std::string& fileName, size_t subscribers = 1)
        : mFileName(fileName.c_str())
        , mSubscribers(subscribers)
    {
    }

    void CreateFile()
    {
        if (!std::filesystem::exists(mFileName.CStr())) {
            std::ofstream file(mFileName.CStr());
        }
    }

    void WriteToFile(const std::string& content)
    {
        auto file = std::ofstream(mFileName.CStr());
        if (!file.is_open()) {
            throw std::runtime_error(std::string("Failed to open test file: ").append(mFileName.CStr()));
        }

        file << content;
    }

    Error WaitForNotification(
        std::vector<fs::FSEvent>& events, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        for (auto& subscriber : mSubscribers) {
            auto err = subscriber.WaitForEvent(mFileName.CStr(), events, timeout);
            if (!err.IsNone()) {
                return err;
            }
        }
        return ErrorEnum::eNone;
    }

    StaticString<cFilePathLen>       mFileName;
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
    FSWatcher mFSWatcher;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FSWatcherTest, StopStart)
{
    ASSERT_TRUE(mFSWatcher.Init(cPollTimeout, cWatchedEvents).IsNone());

    ASSERT_TRUE(mFSWatcher.Stop().Is(ErrorEnum::eWrongState));

    ASSERT_TRUE(mFSWatcher.Start().IsNone());

    ASSERT_TRUE(mFSWatcher.Start().Is(ErrorEnum::eWrongState));

    ASSERT_TRUE(mFSWatcher.Stop().IsNone());
}

TEST_F(FSWatcherTest, StartFailsIfObjectNotInitialized)
{
    auto err = mFSWatcher.Start();
    EXPECT_FALSE(err.IsNone()) << "unexpected error: " << tests::utils::ErrorToStr(err);

    err = mFSWatcher.Stop();
    EXPECT_FALSE(err.IsNone()) << "unexpected error: " << tests::utils::ErrorToStr(err);
}

TEST_F(FSWatcherTest, WatchMultipleFiles)
{
    ASSERT_TRUE(mFSWatcher.Init(cPollTimeout, cWatchedEvents).IsNone());

    ASSERT_TRUE(mFSWatcher.Start().IsNone());

    TestParams params[] = {
        TestParams((cTestDir / "file1.txt").string(), 3),
        TestParams((cTestDir / "file2.txt").string(), 3),
        TestParams((cTestDir / "file3.txt").string(), 3),
    };

    for (auto& param : params) {
        param.CreateFile();

        for (auto& subscriber : param.mSubscribers) {
            ASSERT_TRUE(mFSWatcher.Subscribe(param.mFileName, subscriber).IsNone());
        }
    }

    for (auto& param : params) {
        param.WriteToFile("Initial content");
    }

    for (auto& param : params) {
        std::vector<fs::FSEvent> events;

        auto err = param.WaitForNotification(events);
        EXPECT_TRUE(err.IsNone());

        EXPECT_NE(std::find(events.begin(), events.end(), fs::FSEventEnum::eModify), events.end());
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
        std::vector<fs::FSEvent> events;

        auto err = param.WaitForNotification(events);
        EXPECT_TRUE(err.IsNone());

        EXPECT_NE(std::find(events.begin(), events.end(), fs::FSEventEnum::eModify), events.end());

        for (auto& subscriber : param.mSubscribers) {
            EXPECT_TRUE(mFSWatcher.Unsubscribe(param.mFileName, subscriber).IsNone());
        }
    }

    ASSERT_TRUE(mFSWatcher.Stop().IsNone());
}

TEST_F(FSWatcherTest, BufferedNotification)
{
    const std::vector<fs::FSEvent> cWatchedEvents = {fs::FSEventEnum::eModify, fs::FSEventEnum::eClose};

    FSBufferedWatcher watcher;

    ASSERT_TRUE(watcher.Init(Time::cMilliseconds * 100, Time::cSeconds, cWatchedEvents).IsNone());

    ASSERT_TRUE(watcher.Start().IsNone());

    auto param = TestParams((cTestDir / "file1.txt").string(), 3);

    param.CreateFile();

    for (auto& subscriber : param.mSubscribers) {
        auto err = watcher.Subscribe(param.mFileName, subscriber);
        ASSERT_TRUE(err.IsNone()) << "subscribe failed: " << tests::utils::ErrorToStr(err);
    }

    param.WriteToFile("Notification 1");
    param.WriteToFile("Notification 2");
    param.WriteToFile("Notification 3");

    std::vector<fs::FSEvent> events;

    auto err = param.WaitForNotification(events);
    EXPECT_TRUE(err.IsNone());

    EXPECT_NE(std::find(events.begin(), events.end(), fs::FSEventEnum::eModify), events.end());
    EXPECT_NE(std::find(events.begin(), events.end(), fs::FSEventEnum::eClose), events.end());

    for (auto& subscriber : param.mSubscribers) {
        EXPECT_TRUE(watcher.Unsubscribe(param.mFileName, subscriber).IsNone());
    }

    ASSERT_TRUE(watcher.Stop().IsNone());
}

TEST_F(FSWatcherTest, BufferedNotificationNotSentBeforeTimeout)
{
    constexpr auto                 cNotifyTimeout = Time::cSeconds * 2;
    const std::vector<fs::FSEvent> cWatchedEvents = {fs::FSEventEnum::eModify, fs::FSEventEnum::eClose};

    FSBufferedWatcher watcher;

    ASSERT_TRUE(watcher.Init(Time::cMilliseconds * 100, cNotifyTimeout, cWatchedEvents).IsNone());

    ASSERT_TRUE(watcher.Start().IsNone());

    auto param = TestParams((cTestDir / "file1.txt").string(), 1);

    param.CreateFile();

    for (auto& subscriber : param.mSubscribers) {
        auto err = watcher.Subscribe(param.mFileName, subscriber);
        ASSERT_TRUE(err.IsNone()) << "subscribe failed: " << tests::utils::ErrorToStr(err);
    }

    param.WriteToFile("Notification 1");
    param.WriteToFile("Notification 2");
    param.WriteToFile("Notification 3");

    std::vector<fs::FSEvent> events;

    auto err = param.WaitForNotification(events, std::chrono::milliseconds(100));
    EXPECT_TRUE(err.Is(ErrorEnum::eTimeout));

    err = param.WaitForNotification(events, std::chrono::milliseconds(100));
    EXPECT_TRUE(err.Is(ErrorEnum::eTimeout));

    err = param.WaitForNotification(events, std::chrono::milliseconds(100));
    EXPECT_TRUE(err.Is(ErrorEnum::eTimeout));

    err = param.WaitForNotification(events, std::chrono::milliseconds(2 * cNotifyTimeout.Milliseconds()));
    EXPECT_TRUE(err.IsNone());

    EXPECT_NE(std::find(events.begin(), events.end(), fs::FSEventEnum::eModify), events.end());
    EXPECT_NE(std::find(events.begin(), events.end(), fs::FSEventEnum::eClose), events.end());

    for (auto& subscriber : param.mSubscribers) {
        EXPECT_TRUE(watcher.Unsubscribe(param.mFileName, subscriber).IsNone());
    }

    ASSERT_TRUE(watcher.Stop().IsNone());
}

} // namespace aos::common::utils::test
