/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <aos/test/log.hpp>
#include <aos/test/utils.hpp>

#include <common/cloudprotocol/envvars.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolEnvVars : public Test {
public:
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolEnvVars, EmptyOverrideEnvVarsRequest)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto envVars = std::make_unique<aos::cloudprotocol::OverrideEnvVarsRequest>();

    auto err = ToJSON(*envVars, *json);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "overrideEnvVars");
    EXPECT_TRUE(wrapper.Has("items"));

    auto unparsedEnvVars = std::make_unique<aos::cloudprotocol::OverrideEnvVarsRequest>();

    err = FromJSON(wrapper, *unparsedEnvVars);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    EXPECT_EQ(*unparsedEnvVars, *envVars);
}

TEST_F(CloudProtocolEnvVars, OverrideEnvVarsRequest)
{
    const auto cTime = Time::Unix(1706702400);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto envVars = std::make_unique<aos::cloudprotocol::OverrideEnvVarsRequest>();

    envVars->mItems.EmplaceBack();
    envVars->mItems.Back().mFilter.mInstance.SetValue(12);
    envVars->mItems.Back().mFilter.mServiceID.SetValue("service1");
    envVars->mItems.Back().mFilter.mSubjectID.SetValue("subject1");

    envVars->mItems.Back().mVariables.EmplaceBack();
    envVars->mItems.Back().mVariables.Back().mName.Assign("var1");
    envVars->mItems.Back().mVariables.Back().mValue.Assign("value1");
    envVars->mItems.Back().mVariables.Back().mTTL.SetValue(cTime);

    envVars->mItems.Back().mVariables.EmplaceBack();
    envVars->mItems.Back().mVariables.Back().mName.Assign("var2");
    envVars->mItems.Back().mVariables.Back().mValue.Assign("");

    envVars->mItems.Back().mVariables.EmplaceBack();

    envVars->mItems.EmplaceBack();
    envVars->mItems.Back().mFilter.mInstance.SetValue(13);
    envVars->mItems.Back().mFilter.mServiceID.SetValue("service2");
    envVars->mItems.Back().mFilter.mSubjectID.SetValue("subject2");

    auto err = ToJSON(*envVars, *json);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "overrideEnvVars");
    EXPECT_TRUE(wrapper.Has("items"));

    auto unparsedEnvVars = std::make_unique<aos::cloudprotocol::OverrideEnvVarsRequest>();

    err = FromJSON(wrapper, *unparsedEnvVars);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    EXPECT_EQ(*unparsedEnvVars, *envVars);
}

TEST_F(CloudProtocolEnvVars, EmptyOverrideEnvVarsStatuses)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto envVars = std::make_unique<aos::cloudprotocol::OverrideEnvVarsStatuses>();

    auto err = ToJSON(*envVars, *json);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "overrideEnvVarsStatus");
    EXPECT_TRUE(wrapper.Has("statuses"));

    auto unparsedEnvVars = std::make_unique<aos::cloudprotocol::OverrideEnvVarsStatuses>();

    err = FromJSON(wrapper, *unparsedEnvVars);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    EXPECT_EQ(*unparsedEnvVars, *envVars);
}

TEST_F(CloudProtocolEnvVars, OverrideEnvVarsStatuses)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto envVars = std::make_unique<aos::cloudprotocol::OverrideEnvVarsStatuses>();

    envVars->mStatuses.EmplaceBack();
    envVars->mStatuses.Back().mFilter.mInstance.SetValue(12);
    envVars->mStatuses.Back().mFilter.mServiceID.SetValue("service1");
    envVars->mStatuses.Back().mFilter.mSubjectID.SetValue("subject1");

    envVars->mStatuses.Back().mStatuses.EmplaceBack();
    envVars->mStatuses.Back().mStatuses.Back().mName.Assign("var1");
    envVars->mStatuses.Back().mStatuses.Back().mError = ErrorEnum::eFailed;

    envVars->mStatuses.Back().mStatuses.EmplaceBack();
    envVars->mStatuses.Back().mStatuses.Back().mName.Assign("var2");

    envVars->mStatuses.Back().mStatuses.EmplaceBack();

    envVars->mStatuses.EmplaceBack();
    envVars->mStatuses.Back().mFilter.mInstance.SetValue(13);
    envVars->mStatuses.Back().mFilter.mServiceID.SetValue("service2");
    envVars->mStatuses.Back().mFilter.mSubjectID.SetValue("subject2");

    auto err = ToJSON(*envVars, *json);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "overrideEnvVarsStatus");
    EXPECT_TRUE(wrapper.Has("statuses"));

    auto unparsedEnvVars = std::make_unique<aos::cloudprotocol::OverrideEnvVarsStatuses>();

    err = FromJSON(wrapper, *unparsedEnvVars);
    ASSERT_TRUE(err.IsNone()) << test::ErrorToStr(err);

    EXPECT_EQ(*unparsedEnvVars, *envVars);
}

} // namespace aos::common::cloudprotocol
