/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <aos/test/log.hpp>
#include <aos/test/utils.hpp>

#include <common/cloudprotocol/log.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolLog : public Test {
public:
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolLog, EmptyPushLog)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto pushLog = std::make_unique<aos::cloudprotocol::PushLog>();

    auto err = ToJSON(*pushLog, *json);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "pushLog");
    EXPECT_EQ(wrapper.GetValue<std::string>("logId"), "");
    EXPECT_EQ(wrapper.GetValue<std::string>("nodeId"), "");
    EXPECT_EQ(wrapper.GetValue<uint64_t>("part"), 0);
    EXPECT_EQ(wrapper.GetValue<uint64_t>("partsCount"), 0);
    EXPECT_EQ(wrapper.GetValue<std::string>("content"), "");
    EXPECT_EQ(wrapper.GetValue<std::string>("status"), aos::cloudprotocol::LogStatus().ToString().CStr());
    EXPECT_FALSE(wrapper.Has("errorInfo"));

    auto unparsedPushLog = std::make_unique<aos::cloudprotocol::PushLog>();

    err = FromJSON(wrapper, *unparsedPushLog);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    EXPECT_EQ(*unparsedPushLog, *pushLog);
}

TEST_F(CloudProtocolLog, PushLog)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto pushLog = std::make_unique<aos::cloudprotocol::PushLog>();

    pushLog->mNodeID     = "node1";
    pushLog->mLogID      = "log1";
    pushLog->mPart       = 1;
    pushLog->mPartsCount = 3;
    pushLog->mContent    = "This is a test log content";
    pushLog->mStatus     = aos::cloudprotocol::LogStatusEnum::eError;
    pushLog->mErrorInfo  = Error(ErrorEnum::eFailed, "test error");

    auto err = ToJSON(*pushLog, *json);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    auto unparsedPushLog = std::make_unique<aos::cloudprotocol::PushLog>();

    err = FromJSON(utils::CaseInsensitiveObjectWrapper(json), *unparsedPushLog);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    EXPECT_EQ(*unparsedPushLog, *pushLog);
}

TEST_F(CloudProtocolLog, EmptyLogRequest)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto requestLog = std::make_unique<aos::cloudprotocol::RequestLog>();

    auto err = ToJSON(*requestLog, *json);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "requestLog");
    EXPECT_EQ(wrapper.GetValue<std::string>("logId"), "");
    EXPECT_EQ(wrapper.GetValue<std::string>("logType"), aos::cloudprotocol::LogType().ToString().CStr());
    EXPECT_TRUE(wrapper.Has("filter"));
    EXPECT_FALSE(wrapper.Has("uploadOptions"));

    auto unparsedRequestLog = std::make_unique<aos::cloudprotocol::RequestLog>();

    err = FromJSON(wrapper, *unparsedRequestLog);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    EXPECT_EQ(*unparsedRequestLog, *requestLog);
}

TEST_F(CloudProtocolLog, LogRequest)
{
    const auto cTime = Time::Unix(1706702400);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto requestLog = std::make_unique<aos::cloudprotocol::RequestLog>();

    requestLog->mLogID.Assign("log1");
    requestLog->mLogType = aos::cloudprotocol::LogTypeEnum::eCrashLog;

    requestLog->mFilter.mFrom.SetValue(cTime.Add(Time::cMinutes));
    requestLog->mFilter.mTill.SetValue(cTime.Add(Time::cHours));
    requestLog->mFilter.mNodeIDs.EmplaceBack("node1");
    requestLog->mFilter.mNodeIDs.EmplaceBack("node2");
    requestLog->mFilter.mInstanceFilter.mServiceID.SetValue("service1");
    requestLog->mFilter.mInstanceFilter.mSubjectID.SetValue("subject1");
    requestLog->mFilter.mInstanceFilter.mInstance.SetValue(40);

    requestLog->mUploadOptions.EmplaceValue();
    requestLog->mUploadOptions->mType = aos::cloudprotocol::LogUploadTypeEnum::eHTTPS;
    requestLog->mUploadOptions->mURL.Assign("https://example.com/upload");
    requestLog->mUploadOptions->mBearerToken.Assign("Bearer token123");
    requestLog->mUploadOptions->mBearerTokenTTL.SetValue(cTime.Add(Time::cDay));

    auto err = ToJSON(*requestLog, *json);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    auto unparsedRequestLog = std::make_unique<aos::cloudprotocol::RequestLog>();

    err = FromJSON(utils::CaseInsensitiveObjectWrapper(json), *unparsedRequestLog);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    EXPECT_EQ(*unparsedRequestLog, *requestLog);
}

} // namespace aos::common::cloudprotocol
