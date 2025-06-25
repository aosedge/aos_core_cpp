/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <condition_variable>
#include <gtest/gtest.h>

#include <Poco/InflatingStream.h>
#include <Poco/StreamCopier.h>

#include <aos/test/log.hpp>

#include <sm/alerts/journalalerts.hpp>
#include <sm/logprovider/logprovider.hpp>
#include <sm/tests/mocks/logprovidermock.hpp>

#include "stubs/journalstub.hpp"

using namespace testing;

namespace aos::sm::logprovider {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class TestLogProvider : public LogProvider {
public:
    std::shared_ptr<utils::JournalItf> CreateJournal() override
    {
        return std::shared_ptr<utils::JournalItf>(&mJournal, [](utils::JournalItf*) {});
    }

    utils::JournalStub mJournal;
};

class LogProviderTest : public Test {
public:
    void SetUp() override
    {
        aos::test::InitLog();

        auto config = aos::logprovider::Config {200, 10};

        mLogProvider.Init(config, mInstanceIDProvider);
        mLogProvider.Subscribe(mLogObserver);
        mLogProvider.Start();
    }

    void TearDown() override { mLogProvider.Stop(); }

protected:
    static constexpr auto cAOSServiceSlicePrefix = "/system.slice/system-aos@service.slice/";

    std::function<Error(const cloudprotocol::PushLog&)> GetLogReceivedNotifier()
    {
        auto notifier = [this](const cloudprotocol::PushLog& log) {
            (void)log;

            mLogReceived.notify_all();

            return Error();
        };

        return notifier;
    }

    void WaitLogReceived()
    {
        std::unique_lock lock {mMutex};

        mLogReceived.wait_for(lock, std::chrono::seconds(1));
    }

    TestLogProvider mLogProvider;

    InstanceIDProviderMock mInstanceIDProvider;
    LogObserverMock        mLogObserver;

    std::mutex              mMutex;
    std::condition_variable mLogReceived;
};

cloudprotocol::InstanceFilter CreateInstanceFilter(
    const std::string& serviceID, const std::string& subjectID, uint64_t instance)
{
    cloudprotocol::InstanceFilter filter;

    filter.mServiceID.EmplaceValue(serviceID.c_str());
    filter.mSubjectID.EmplaceValue(subjectID.c_str());
    filter.mInstance.EmplaceValue(instance);

    return filter;
}

std::string UnzipData(const String& compressedData)
{
    std::stringstream outputStream;
    std::stringstream inputStream;

    Poco::InflatingOutputStream inflater(outputStream, Poco::InflatingStreamBuf::STREAM_GZIP);

    inputStream.write(compressedData.begin(), compressedData.Size());

    Poco::StreamCopier::copyStream(inputStream, inflater);
    inflater.close();

    return outputStream.str();
}

MATCHER_P5(MatchPushLog, logID, partsCount, part, content, status, "PushLog matcher")
{
    auto data = UnzipData(arg.mContent);

    return (arg.mLogID == logID && arg.mPartsCount == partsCount && arg.mPart == part
        && data.find(content) != std::string::npos && arg.mStatus == status);
}

MATCHER_P(MatchEmptyPushLog, logID, "PushLog empty matcher")
{
    return (arg.mLogID == logID && arg.mPartsCount == 1 && arg.mPart == 1 && arg.mContent.IsEmpty()
        && arg.mStatus == cloudprotocol::LogStatusEnum::eEmpty);
}

MATCHER_P(MatchAbsentPushLog, logID, "PushLog absent matcher")
{
    return (arg.mLogID == logID && arg.mPartsCount == 1 && arg.mPart == 1 && arg.mContent.IsEmpty()
        && arg.mStatus == cloudprotocol::LogStatusEnum::eAbsent);
}

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(LogProviderTest, GetServiceLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto instanceFilter = CreateInstanceFilter("logservice0", "subject0", 0);
    auto unitName       = "aos-service@logservice0.service";

    mLogProvider.mJournal.AddMessage("This is log", unitName, "");

    cloudprotocol::RequestLog request = {};
    request.mLogID                    = "log0";
    request.mFilter                   = cloudprotocol::LogFilter {from, till, {}, {}, instanceFilter};

    std::vector<std::string> instanceIDs = {"logservice0"};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(instanceFilter))
        .WillOnce(Return(RetWithError<std::vector<std::string>>(instanceIDs, ErrorEnum::eNone)));

    EXPECT_CALL(
        mLogObserver, OnLogReceived(MatchPushLog("log0", 1U, 1U, "This is log", cloudprotocol::LogStatusEnum::eOk)))
        .WillOnce(Invoke(GetLogReceivedNotifier()));
    EXPECT_TRUE(mLogProvider.GetInstanceLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetBigServiceLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto instanceFilter = CreateInstanceFilter("logservice0", "subject0", 0);
    auto unitName       = "aos-service@logservice0.service";

    for (int i = 0; i < 10; i++) {
        mLogProvider.mJournal.AddMessage("Hello World", unitName, "");
    }

    cloudprotocol::RequestLog request = {};
    request.mLogID                    = "log0";
    request.mFilter                   = cloudprotocol::LogFilter {from, till, {}, {}, instanceFilter};

    std::vector<std::string> instanceIDs = {"logservice0"};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(instanceFilter))
        .WillOnce(Return(RetWithError<std::vector<std::string>>(instanceIDs, ErrorEnum::eNone)));

    EXPECT_CALL(mLogObserver, OnLogReceived(MatchPushLog("log0", 2U, 1U, "", cloudprotocol::LogStatusEnum::eOk)));
    EXPECT_CALL(mLogObserver, OnLogReceived(MatchPushLog("log0", 2U, 2U, "", cloudprotocol::LogStatusEnum::eOk)))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetSystemLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    for (int i = 0; i < 5; i++) {
        mLogProvider.mJournal.AddMessage("Hello World", "logger", "");
    }

    cloudprotocol::RequestLog request = {};
    request.mLogID                    = "log0";
    request.mFilter                   = cloudprotocol::LogFilter {from, till, {}, {}, {}};

    EXPECT_CALL(
        mLogObserver, OnLogReceived(MatchPushLog("log0", 1U, 1U, "Hello World", cloudprotocol::LogStatusEnum::eOk)))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetSystemLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetEmptyLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto instanceFilter = CreateInstanceFilter("logservice0", "subject0", 0);

    cloudprotocol::RequestLog request = {};
    request.mLogID                    = "log0";
    request.mFilter                   = cloudprotocol::LogFilter {from, till, {}, {}, instanceFilter};

    std::vector<std::string> instanceIDs = {"logservice0"};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(instanceFilter))
        .WillOnce(Return(RetWithError<std::vector<std::string>>(instanceIDs, ErrorEnum::eNone)));

    EXPECT_CALL(mLogObserver, OnLogReceived(MatchEmptyPushLog("log0"))).WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetCrashLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto instanceFilter = CreateInstanceFilter("logservice0", "subject0", 0);
    auto unitName       = std::string("aos-service@logservice0.service");

    mLogProvider.mJournal.AddMessage("Started", unitName, cAOSServiceSlicePrefix + unitName);
    mLogProvider.mJournal.AddMessage("somelog1", unitName, cAOSServiceSlicePrefix + unitName);
    mLogProvider.mJournal.AddMessage("somelog3", unitName, cAOSServiceSlicePrefix + unitName);
    mLogProvider.mJournal.AddMessage("process exited", unitName, cAOSServiceSlicePrefix + unitName);
    sleep(1);
    mLogProvider.mJournal.AddMessage("skip log", unitName, cAOSServiceSlicePrefix + unitName);

    cloudprotocol::RequestLog request = {};
    request.mLogID                    = "log0";
    request.mFilter                   = cloudprotocol::LogFilter {from, till, {}, {}, instanceFilter};

    std::vector<std::string> instanceIDs = {"logservice0"};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(instanceFilter))
        .WillOnce(Return(RetWithError<std::vector<std::string>>(instanceIDs, ErrorEnum::eNone)));

    EXPECT_CALL(mLogObserver,
        OnLogReceived(AllOf(MatchPushLog("log0", 1U, 1U, "somelog1", cloudprotocol::LogStatusEnum::eOk),
            MatchPushLog("log0", 1U, 1U, "somelog3", cloudprotocol::LogStatusEnum::eOk),
            MatchPushLog("log0", 1U, 1U, "process exited", cloudprotocol::LogStatusEnum::eOk))))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceCrashLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetInstanceIDsFailed)
{
    auto                     from        = Time::Now();
    auto                     till        = from.Add(5 * Time::cSeconds);
    std::vector<std::string> instanceIDs = {};

    auto instanceFilter = CreateInstanceFilter("logservice0", "subject0", 0);
    auto unitName       = std::string("aos-service@logservice0.service");

    cloudprotocol::RequestLog request = {};

    request.mLogID  = "log0";
    request.mFilter = cloudprotocol::LogFilter {from, till, {}, {}, instanceFilter};

    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(instanceFilter))
        .WillOnce(Return(RetWithError<std::vector<std::string>>(instanceIDs, ErrorEnum::eFailed)));

    EXPECT_CALL(mLogObserver, OnLogReceived(Field(&cloudprotocol::PushLog::mErrorInfo, Error(ErrorEnum::eFailed))))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_FALSE(mLogProvider.GetInstanceCrashLog(request).IsNone());

    // notification is instant, no need to wait.

    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(instanceFilter))
        .WillOnce(Return(RetWithError<std::vector<std::string>>(instanceIDs, ErrorEnum::eFailed)));

    EXPECT_CALL(mLogObserver, OnLogReceived(Field(&cloudprotocol::PushLog::mErrorInfo, Error(ErrorEnum::eFailed))))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_FALSE(mLogProvider.GetInstanceLog(request).IsNone());

    // notification is instant, no need to wait.
}

TEST_F(LogProviderTest, EmptyInstanceIDs)
{
    auto                     from        = Time::Now();
    auto                     till        = from.Add(5 * Time::cSeconds);
    std::vector<std::string> instanceIDs = {};

    auto instanceFilter = CreateInstanceFilter("logservice0", "subject0", 0);
    auto unitName       = std::string("aos-service@logservice0.service");

    cloudprotocol::RequestLog request = {};

    request.mLogID  = "log0";
    request.mFilter = cloudprotocol::LogFilter {from, till, {}, {}, instanceFilter};

    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(instanceFilter))
        .WillOnce(Return(RetWithError<std::vector<std::string>>(instanceIDs, ErrorEnum::eNone)));

    EXPECT_CALL(mLogObserver, OnLogReceived(MatchAbsentPushLog("log0"))).WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceCrashLog(request).IsNone());

    // notification is instant, no need to wait.

    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(instanceFilter))
        .WillOnce(Return(RetWithError<std::vector<std::string>>(instanceIDs, ErrorEnum::eNone)));

    EXPECT_CALL(mLogObserver, OnLogReceived(MatchAbsentPushLog("log0"))).WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceLog(request).IsNone());

    // notification is instant, no need to wait.
}

} // namespace aos::sm::logprovider
