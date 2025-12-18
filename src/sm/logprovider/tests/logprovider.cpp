/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <condition_variable>
#include <gtest/gtest.h>

#include <Poco/InflatingStream.h>
#include <Poco/StreamCopier.h>

#include <core/common/tests/utils/log.hpp>

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
        aos::tests::utils::InitLog();

        auto config = aos::logging::Config {200, 10};

        mLogProvider.Init(config, mInstanceIDProvider);
        mLogProvider.Subscribe(mLogSender);
        mLogProvider.Start();
    }

    void TearDown() override { mLogProvider.Stop(); }

protected:
    static constexpr auto cAOSServiceSlicePrefix = "/system.slice/system-aos@service.slice/";

    std::function<Error(const PushLog&)> GetLogReceivedNotifier()
    {
        auto notifier = [this](const PushLog& log) {
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
    LogSenderMock          mLogSender;

    std::mutex              mMutex;
    std::condition_variable mLogReceived;
};

LogFilter CreateLogFilter(
    const std::string& serviceID, const std::string& subjectID, uint64_t instance, Time from, Time till)
{
    LogFilter filter;

    filter.mItemID.EmplaceValue(serviceID.c_str());
    filter.mSubjectID.EmplaceValue(subjectID.c_str());
    filter.mInstance.EmplaceValue(instance);
    filter.mFrom.EmplaceValue(from);
    filter.mTill.EmplaceValue(till);

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

MATCHER_P5(MatchPushLog, correlationID, partsCount, part, content, status, "PushLog matcher")
{
    auto data = UnzipData(arg.mContent);

    return (arg.mCorrelationID == correlationID && arg.mPartsCount == partsCount && arg.mPart == part
        && data.find(content) != std::string::npos && arg.mStatus == status);
}

MATCHER_P(MatchEmptyPushLog, correlationID, "PushLog empty matcher")
{
    return (arg.mCorrelationID == correlationID && arg.mPartsCount == 1 && arg.mPart == 1 && arg.mContent.IsEmpty()
        && arg.mStatus == LogStatusEnum::eEmpty);
}

MATCHER_P(MatchAbsentPushLog, correlationID, "PushLog absent matcher")
{
    return (arg.mCorrelationID == correlationID && arg.mPartsCount == 1 && arg.mPart == 1 && arg.mContent.IsEmpty()
        && arg.mStatus == LogStatusEnum::eAbsent);
}

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(LogProviderTest, GetServiceLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto logFilter = CreateLogFilter("logservice0", "subject0", 0, from, till);
    auto unitName  = "aos-service@logservice0.service";

    mLogProvider.mJournal.AddMessage("This is log", unitName, "");

    RequestLog request     = {};
    request.mCorrelationID = "log0";
    request.mFilter        = logFilter;

    std::vector<std::string> instanceIDs = {"logservice0"};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(_, _))
        .WillOnce(DoAll(SetArgReferee<1>(instanceIDs), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mLogSender, SendLog(MatchPushLog("log0", 1U, 1U, "This is log", LogStatusEnum::eOK)))
        .WillOnce(Invoke(GetLogReceivedNotifier()));
    EXPECT_TRUE(mLogProvider.GetInstanceLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetBigServiceLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto logFilter = CreateLogFilter("logservice0", "subject0", 0, from, till);
    auto unitName  = "aos-service@logservice0.service";

    for (int i = 0; i < 10; i++) {
        mLogProvider.mJournal.AddMessage("Hello World", unitName, "");
    }

    RequestLog request     = {};
    request.mCorrelationID = "log0";
    request.mFilter        = logFilter;

    std::vector<std::string> instanceIDs = {"logservice0"};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(_, _))
        .WillOnce(DoAll(SetArgReferee<1>(instanceIDs), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mLogSender, SendLog(MatchPushLog("log0", 2U, 1U, "", LogStatusEnum::eOK)));
    EXPECT_CALL(mLogSender, SendLog(MatchPushLog("log0", 2U, 2U, "", LogStatusEnum::eOK)))
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

    LogFilter logFilter;
    logFilter.mFrom.EmplaceValue(from);
    logFilter.mTill.EmplaceValue(till);

    RequestLog request     = {};
    request.mCorrelationID = "log0";
    request.mFilter        = logFilter;

    EXPECT_CALL(mLogSender, SendLog(MatchPushLog("log0", 1U, 1U, "Hello World", LogStatusEnum::eOK)))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetSystemLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetEmptyLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto logFilter = CreateLogFilter("logservice0", "subject0", 0, from, till);

    RequestLog request     = {};
    request.mCorrelationID = "log0";
    request.mFilter        = logFilter;

    std::vector<std::string> instanceIDs = {"logservice0"};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(_, _))
        .WillOnce(DoAll(SetArgReferee<1>(instanceIDs), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mLogSender, SendLog(MatchEmptyPushLog("log0"))).WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetCrashLog)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto logFilter = CreateLogFilter("logservice0", "subject0", 0, from, till);
    auto unitName  = std::string("aos-service@logservice0.service");

    mLogProvider.mJournal.AddMessage("Started", unitName, cAOSServiceSlicePrefix + unitName);
    mLogProvider.mJournal.AddMessage("somelog1", unitName, cAOSServiceSlicePrefix + unitName);
    mLogProvider.mJournal.AddMessage("somelog3", unitName, cAOSServiceSlicePrefix + unitName);
    mLogProvider.mJournal.AddMessage("process exited", unitName, cAOSServiceSlicePrefix + unitName);
    sleep(1);
    mLogProvider.mJournal.AddMessage("skip log", unitName, cAOSServiceSlicePrefix + unitName);

    RequestLog request     = {};
    request.mCorrelationID = "log0";
    request.mFilter        = logFilter;

    std::vector<std::string> instanceIDs = {"logservice0"};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(_, _))
        .WillOnce(DoAll(SetArgReferee<1>(instanceIDs), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mLogSender,
        SendLog(AllOf(MatchPushLog("log0", 1U, 1U, "somelog1", LogStatusEnum::eOK),
            MatchPushLog("log0", 1U, 1U, "somelog3", LogStatusEnum::eOK),
            MatchPushLog("log0", 1U, 1U, "process exited", LogStatusEnum::eOK))))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceCrashLog(request).IsNone());

    WaitLogReceived();
}

TEST_F(LogProviderTest, GetInstanceIDsFailed)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto logFilter = CreateLogFilter("logservice0", "subject0", 0, from, till);

    RequestLog request     = {};
    request.mCorrelationID = "log0";
    request.mFilter        = logFilter;

    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(_, _)).WillOnce(Return(ErrorEnum::eFailed));

    EXPECT_CALL(mLogSender, SendLog(Field(&PushLog::mError, Error(ErrorEnum::eFailed))))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_FALSE(mLogProvider.GetInstanceCrashLog(request).IsNone());

    // notification is instant, no need to wait.

    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(_, _)).WillOnce(Return(ErrorEnum::eFailed));

    EXPECT_CALL(mLogSender, SendLog(Field(&PushLog::mError, Error(ErrorEnum::eFailed))))
        .WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_FALSE(mLogProvider.GetInstanceLog(request).IsNone());

    // notification is instant, no need to wait.
}

TEST_F(LogProviderTest, EmptyInstanceIDs)
{
    auto from = Time::Now();
    auto till = from.Add(5 * Time::cSeconds);

    auto logFilter = CreateLogFilter("logservice0", "subject0", 0, from, till);

    RequestLog request     = {};
    request.mCorrelationID = "log0";
    request.mFilter        = logFilter;

    std::vector<std::string> emptyInstanceIDs = {};
    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(_, _))
        .WillOnce(DoAll(SetArgReferee<1>(emptyInstanceIDs), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mLogSender, SendLog(MatchAbsentPushLog("log0"))).WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceCrashLog(request).IsNone());

    // notification is instant, no need to wait.

    EXPECT_CALL(mInstanceIDProvider, GetInstanceIDs(_, _))
        .WillOnce(DoAll(SetArgReferee<1>(emptyInstanceIDs), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mLogSender, SendLog(MatchAbsentPushLog("log0"))).WillOnce(Invoke(GetLogReceivedNotifier()));

    EXPECT_TRUE(mLogProvider.GetInstanceLog(request).IsNone());

    // notification is instant, no need to wait.
}

} // namespace aos::sm::logprovider
