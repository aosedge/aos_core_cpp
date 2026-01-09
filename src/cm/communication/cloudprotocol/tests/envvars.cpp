/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/communication/cloudprotocol/envvars.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolEnvVars : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolEnvVars, OverrideEnvVarsRequest)
{
    const auto cJSON = R"({
        "messageType": "overrideEnvVarsStatus",
        "correlationId": "id",
            "items": [
                {
                    "item": {
                        "id": "itemID"
                    },
                    "subject": {
                        "id": "subjectID"
                    },
                    "instance": 0,
                    "variables": [
                        {
                            "name": "var1",
                            "value": "val1"
                        },
                        {
                            "name": "var2",
                            "value": "val2",
                            "ttl": "2024-01-31T12:00:00Z"
                        }
                    ]
                },
                {
                    "item": {
                        "id": "zeroVars"
                    },
                    "variables": [
                    ]
                }
            ]
    })";

    auto envVars = std::make_unique<OverrideEnvVarsRequest>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *envVars);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(envVars->mCorrelationID.CStr(), "id");
    ASSERT_EQ(envVars->mItems.Size(), 2);

    ASSERT_EQ(envVars->mItems[0].mVariables.Size(), 2);
    EXPECT_STREQ(envVars->mItems[0].mItemID->CStr(), "itemID");
    EXPECT_STREQ(envVars->mItems[0].mSubjectID->CStr(), "subjectID");
    EXPECT_EQ(envVars->mItems[0].mInstance.GetValue(), 0);

    EXPECT_STREQ(envVars->mItems[0].mVariables[0].mName.CStr(), "var1");
    EXPECT_STREQ(envVars->mItems[0].mVariables[0].mValue.CStr(), "val1");
    EXPECT_FALSE(envVars->mItems[0].mVariables[0].mTTL.HasValue());

    EXPECT_STREQ(envVars->mItems[0].mVariables[1].mName.CStr(), "var2");
    EXPECT_STREQ(envVars->mItems[0].mVariables[1].mValue.CStr(), "val2");
    EXPECT_STREQ(envVars->mItems[0].mVariables[1].mTTL->ToUTCString().mValue.CStr(), "2024-01-31T12:00:00Z");

    ASSERT_EQ(envVars->mItems[1].mVariables.Size(), 0);
    EXPECT_STREQ(envVars->mItems[1].mItemID->CStr(), "zeroVars");
    EXPECT_FALSE(envVars->mItems[1].mSubjectID.HasValue());
    EXPECT_FALSE(envVars->mItems[1].mInstance.HasValue());
}

TEST_F(CloudProtocolEnvVars, OverrideEnvVarsStatuses)
{
    constexpr auto cJSON = R"({"messageType":"overrideEnvVarsStatus","correlationId":"id",)"
                           R"("statuses":[{"item":{"id":"itemID"},)"
                           R"("subject":{"id":"subjectID"},"instance":0,"name":"var0","errorInfo":{"aosCode":1,)"
                           R"("exitCode":0,"message":""}},{"item":{"id":"itemID"},"subject":{"id":"subjectID"},)"
                           R"("instance":0,"name":"var1"},{"item":{"id":"itemID"},"subject":{"id":"subjectID"},)"
                           R"("instance":0,"name":"var2"},{"item":{"id":"itemID"},"subject":{"id":"subjectID"},)"
                           R"("instance":1,"name":"var0"}]})";

    auto statuses            = std::make_unique<OverrideEnvVarsStatuses>();
    statuses->mCorrelationID = "id";

    statuses->mStatuses.EmplaceBack();
    statuses->mStatuses.Back().mInstance  = 0;
    statuses->mStatuses.Back().mItemID    = "itemID";
    statuses->mStatuses.Back().mSubjectID = "subjectID";

    statuses->mStatuses.Back().mStatuses.EmplaceBack();
    statuses->mStatuses.Back().mStatuses.Back().mName  = "var0";
    statuses->mStatuses.Back().mStatuses.Back().mError = ErrorEnum::eFailed;

    statuses->mStatuses.Back().mStatuses.EmplaceBack();
    statuses->mStatuses.Back().mStatuses.Back().mName = "var1";

    statuses->mStatuses.Back().mStatuses.EmplaceBack();
    statuses->mStatuses.Back().mStatuses.Back().mName = "var2";

    statuses->mStatuses.EmplaceBack();
    statuses->mStatuses.Back().mInstance  = 1;
    statuses->mStatuses.Back().mItemID    = "itemID";
    statuses->mStatuses.Back().mSubjectID = "subjectID";

    statuses->mStatuses.Back().mStatuses.EmplaceBack();
    statuses->mStatuses.Back().mStatuses.Back().mName = "var0";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*statuses, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

} // namespace aos::cm::communication::cloudprotocol
