/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <vector>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/communication/cloudprotocol/common.hpp>

using namespace testing;

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Operators
 **********************************************************************************************************************/

bool operator==(const AosIdentity& lhs, const AosIdentity& rhs)
{
    return lhs.mID == rhs.mID && lhs.mCodename == rhs.mCodename && lhs.mType == rhs.mType && lhs.mTitle == rhs.mTitle;
}

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolCommon : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolCommon, Error)
{
    constexpr auto cJSON = R"({"aosCode":2,"exitCode":10,"message":"test message"})";

    Error error(10, "test message");
    ASSERT_FALSE(error.IsNone());

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);
    ASSERT_EQ(ToJSON(error, *json), ErrorEnum::eNone);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolCommon, AosIdentity)
{
    const std::vector<AosIdentity> testCases = {
        {{"id"}, {"codename"}, {UpdateItemTypeEnum::eService}, {"title"}},
        {{"id-only"}, {}, {}, {}},
        {{}, {"codename-only"}, {}, {}},
        {{}, {}, {UpdateItemTypeEnum::eComponent}, {}},
        {{}, {}, {}, {"title-only"}},
        {{}, {}, {}, {}},
    };

    for (const auto& identity : testCases) {
        auto json = CreateAosIdentity(identity);
        ASSERT_TRUE(json);

        common::utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

        AosIdentity parsedIdentity;

        auto err = ParseAosIdentity(jsonWrapper, parsedIdentity);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(parsedIdentity, identity);
    }
}

TEST_F(CloudProtocolCommon, InstanceIdentFromJSON)
{
    constexpr auto cJSON = R"({
        "item": {
            "id": "item-id"
        },
        "subject": {
            "id": "subject-id"
        },
        "instance": 42
    })";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    common::utils::CaseInsensitiveObjectWrapper jsonWrapper(jsonVar);

    InstanceIdent instanceIdent;
    err = FromJSON(jsonWrapper, instanceIdent);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(instanceIdent.mItemID.CStr(), "item-id");
    EXPECT_STREQ(instanceIdent.mSubjectID.CStr(), "subject-id");
    EXPECT_EQ(instanceIdent.mInstance, 42);
}

TEST_F(CloudProtocolCommon, InstanceIdent)
{
    constexpr auto cJSON = R"({"item":{"id":"item-id"},"subject":{"id":"subject-id"},"instance":42})";

    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "item-id";
    instanceIdent.mSubjectID = "subject-id";
    instanceIdent.mInstance  = 42;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);
    ASSERT_EQ(ToJSON(instanceIdent, *json), ErrorEnum::eNone);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolCommon, PreinstalledInstanceIdent)
{
    constexpr auto cJSON = R"({"item":{"codename":"item-id"},"subject":{"codename":"subject-id"},"instance":42})";

    InstanceIdent instanceIdent;
    instanceIdent.mItemID       = "item-id";
    instanceIdent.mSubjectID    = "subject-id";
    instanceIdent.mInstance     = 42;
    instanceIdent.mPreinstalled = true;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);
    ASSERT_EQ(ToJSON(instanceIdent, *json), ErrorEnum::eNone);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolCommon, InstanceFilter)
{
    constexpr auto cJSON = R"({"item":{"id":"item-id"},"subject":{"id":"subject-id"},"instance":42})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    common::utils::CaseInsensitiveObjectWrapper jsonWrapper(jsonVar);

    InstanceFilter instanceFilter;

    err = FromJSON(jsonWrapper, instanceFilter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_TRUE(instanceFilter.mItemID.HasValue());
    EXPECT_STREQ(instanceFilter.mItemID->CStr(), "item-id");

    ASSERT_TRUE(instanceFilter.mSubjectID.HasValue());
    EXPECT_STREQ(instanceFilter.mSubjectID->CStr(), "subject-id");

    ASSERT_TRUE(instanceFilter.mInstance.HasValue());
    EXPECT_EQ(*instanceFilter.mInstance, 42);
}

TEST_F(CloudProtocolCommon, InstanceFilterOnlyItemTagExists)
{
    constexpr auto cJSON = R"({"item":{"id":"item-id"}})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    common::utils::CaseInsensitiveObjectWrapper jsonWrapper(jsonVar);

    InstanceFilter instanceFilter;

    err = FromJSON(jsonWrapper, instanceFilter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_TRUE(instanceFilter.mItemID.HasValue());
    EXPECT_STREQ(instanceFilter.mItemID->CStr(), "item-id");

    EXPECT_FALSE(instanceFilter.mSubjectID.HasValue());
    EXPECT_FALSE(instanceFilter.mInstance.HasValue());
}

TEST_F(CloudProtocolCommon, InstanceFilterOnlySubjectTagExists)
{
    constexpr auto cJSON = R"({"subject":{"id":"subject-id"}})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    common::utils::CaseInsensitiveObjectWrapper jsonWrapper(jsonVar);

    InstanceFilter instanceFilter;

    err = FromJSON(jsonWrapper, instanceFilter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_TRUE(instanceFilter.mSubjectID.HasValue());
    EXPECT_STREQ(instanceFilter.mSubjectID->CStr(), "subject-id");

    EXPECT_FALSE(instanceFilter.mItemID.HasValue());
    EXPECT_FALSE(instanceFilter.mInstance.HasValue());
}

TEST_F(CloudProtocolCommon, InstanceFilterOnlyInstanceTagExists)
{
    constexpr auto cJSON = R"({"instance":42})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    common::utils::CaseInsensitiveObjectWrapper jsonWrapper(jsonVar);

    InstanceFilter instanceFilter;

    err = FromJSON(jsonWrapper, instanceFilter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_TRUE(instanceFilter.mInstance.HasValue());
    EXPECT_EQ(*instanceFilter.mInstance, 42);

    EXPECT_FALSE(instanceFilter.mItemID.HasValue());
    EXPECT_FALSE(instanceFilter.mSubjectID.HasValue());
}

TEST_F(CloudProtocolCommon, InstanceFilterNoFilterTags)
{
    constexpr auto cJSON = R"({})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    common::utils::CaseInsensitiveObjectWrapper jsonWrapper(jsonVar);

    InstanceFilter instanceFilter;

    err = FromJSON(jsonWrapper, instanceFilter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_FALSE(instanceFilter.mItemID.HasValue());
    EXPECT_FALSE(instanceFilter.mSubjectID.HasValue());
    EXPECT_FALSE(instanceFilter.mInstance.HasValue());
}

TEST_F(CloudProtocolCommon, ProtocolToJSON)
{
    const auto     cProtocol = Protocol {"corr-id"};
    constexpr auto cJSON     = R"({"correlationId":"corr-id"})";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);
    ASSERT_EQ(ToJSON(cProtocol, *json), ErrorEnum::eNone);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolCommon, ProtocolFromJSON)
{
    constexpr auto cJSON = R"({"correlationId":"corr-id"})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    common::utils::CaseInsensitiveObjectWrapper jsonWrapper(jsonVar);

    Protocol protocol;

    err = FromJSON(jsonWrapper, protocol);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(protocol.mCorrelationID.CStr(), "corr-id");
}

} // namespace aos::cm::communication::cloudprotocol
