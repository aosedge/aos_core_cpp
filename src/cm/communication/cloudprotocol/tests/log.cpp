/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/communication/cloudprotocol/log.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolLog : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolLog, RequestLog)
{
    constexpr auto cJSON = R"({
        "messageType": "requestLog",
        "correlationID": "logID",
        "logType": "systemLog",
        "filter": {
            "from": "2024-01-01T12:00:00Z",
            "till": "2024-01-31T12:00:00Z",
            "nodeIds": [
                {
                    "codename": "node1"
                },
                {
                    "codename": "node2"
                }
            ],
            "item": {
                "id": "itemID"
            },
            "subject": {
                "id": "subjectID"
            },
            "instance": 1
        }
    })";

    auto requestLog = std::make_unique<RequestLog>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    common::utils::CaseInsensitiveObjectWrapper jsonWrapper(jsonVar);

    err = FromJSON(jsonWrapper, *requestLog);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(requestLog->mCorrelationID, "logID");
    EXPECT_EQ(requestLog->mLogType.GetValue(), LogTypeEnum::eSystemLog);
    EXPECT_STREQ(requestLog->mFilter.mFrom->ToUTCString().mValue.CStr(), "2024-01-01T12:00:00Z");
    EXPECT_STREQ(requestLog->mFilter.mTill->ToUTCString().mValue.CStr(), "2024-01-31T12:00:00Z");

    ASSERT_EQ(requestLog->mFilter.mNodes.Size(), 2);
    EXPECT_STREQ(requestLog->mFilter.mNodes[0].CStr(), "node1");
    EXPECT_STREQ(requestLog->mFilter.mNodes[1].CStr(), "node2");

    EXPECT_STREQ(requestLog->mFilter.mItemID->CStr(), "itemID");
    EXPECT_STREQ(requestLog->mFilter.mSubjectID->CStr(), "subjectID");
    EXPECT_EQ(requestLog->mFilter.mInstance.GetValue(), 1);
}

TEST_F(CloudProtocolLog, PushLog)
{
    constexpr auto cJSON = R"({"messageType":"pushLog","correlationID":"logID","node":{"codename":"nodeID"},)"
                           R"("part":1,"partsCount":10,"content":"log content","status":"error",)"
                           R"("errorInfo":{"aosCode":1,"exitCode":0,"message":""}})";

    auto pushLog = std::make_unique<PushLog>();

    pushLog->mCorrelationID = "logID";
    pushLog->mNodeID        = "nodeID";
    pushLog->mPart          = 1;
    pushLog->mPartsCount    = 10;
    pushLog->mContent       = "log content";
    pushLog->mStatus        = LogStatusEnum::eError;
    pushLog->mError         = ErrorEnum::eFailed;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*pushLog, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

} // namespace aos::cm::communication::cloudprotocol
