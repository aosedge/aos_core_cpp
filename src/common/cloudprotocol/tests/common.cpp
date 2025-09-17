/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/common/tools/uuid.hpp>

#include <common/cloudprotocol/common.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

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
    Error error(10, "test message");
    ASSERT_FALSE(error.IsNone());

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(error, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);
    EXPECT_EQ(jsonWrapper.GetValue<int>("aosCode", -1), static_cast<int>(error.Value()));
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("message", "unexpected"), "test message");
    EXPECT_EQ(jsonWrapper.GetValue<int>("errno", -1), 10);

    Error parsedError;
    ASSERT_EQ(FromJSON(jsonWrapper, parsedError), ErrorEnum::eNone);

    ASSERT_EQ(error, parsedError);
}

TEST_F(CloudProtocolCommon, InstanceIdent)
{
    InstanceIdent instanceIdent {"service1", "subject1", 42};

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(instanceIdent, *json), ErrorEnum::eNone);

    InstanceIdent parsedInstanceIdent;
    ASSERT_EQ(FromJSON(utils::CaseInsensitiveObjectWrapper(json), parsedInstanceIdent), ErrorEnum::eNone);

    ASSERT_EQ(instanceIdent, parsedInstanceIdent);
}

TEST_F(CloudProtocolCommon, InstanceFilter)
{
    const std::array instanceFilters = {
        aos::InstanceFilter {{}, {}, {}},
        aos::InstanceFilter {{"service1"}, {}, {}},
        aos::InstanceFilter {{"service1"}, {"subject1"}, {}},
        aos::InstanceFilter {{"service1"}, {"subject1"}, 42},
    };

    for (const auto& filter : instanceFilters) {
        auto json = Poco::makeShared<Poco::JSON::Object>();

        EXPECT_EQ(ToJSON(filter, *json), ErrorEnum::eNone);

        aos::InstanceFilter parsedFilter;
        EXPECT_TRUE(FromJSON(utils::CaseInsensitiveObjectWrapper(json), parsedFilter).IsNone());

        EXPECT_EQ(filter, parsedFilter);
    }
}

TEST_F(CloudProtocolCommon, Identifier)
{
    const std::array identifiers = {
        aos::cloudprotocol::Identifier {},
        aos::cloudprotocol::Identifier {
            uuid::StringToUUID("00000000-0000-0000-0000-000000000001").mValue, {}, {}, {}, {}, {}},
        aos::cloudprotocol::Identifier {uuid::StringToUUID("00000000-0000-0000-0000-000000000002").mValue,
            UpdateItemType(UpdateItemTypeEnum::eService), {}, {}, {}, {}},
        aos::cloudprotocol::Identifier {uuid::StringToUUID("00000000-0000-0000-0000-000000000002").mValue,
            UpdateItemType(UpdateItemTypeEnum::eService), StaticString<aos::cloudprotocol::cCodeNameLen>("codeName"),
            {}, {}, {}},
        aos::cloudprotocol::Identifier {uuid::StringToUUID("00000000-0000-0000-0000-000000000002").mValue,
            UpdateItemType(UpdateItemTypeEnum::eService), StaticString<aos::cloudprotocol::cCodeNameLen>("codeName"),
            StaticString<aos::cloudprotocol::cTitleLen>("title"), {}, {}},
        aos::cloudprotocol::Identifier {uuid::StringToUUID("00000000-0000-0000-0000-000000000002").mValue,
            UpdateItemType(UpdateItemTypeEnum::eService), StaticString<aos::cloudprotocol::cCodeNameLen>("codeName"),
            StaticString<aos::cloudprotocol::cTitleLen>("title"),
            StaticString<aos::cloudprotocol::cDescriptionLen>("description"), {}},
        aos::cloudprotocol::Identifier {uuid::StringToUUID("00000000-0000-0000-0000-000000000002").mValue,
            UpdateItemType(UpdateItemTypeEnum::eService), StaticString<aos::cloudprotocol::cCodeNameLen>("codeName"),
            StaticString<aos::cloudprotocol::cTitleLen>("title"),
            StaticString<aos::cloudprotocol::cDescriptionLen>("description"),
            StaticString<aos::cloudprotocol::cURNLen>("urn")},
    };

    for (const auto& identifier : identifiers) {
        auto json = Poco::makeShared<Poco::JSON::Object>();

        EXPECT_EQ(ToJSON(identifier, *json), ErrorEnum::eNone);

        aos::cloudprotocol::Identifier parsedIdentifier;
        EXPECT_TRUE(FromJSON(utils::CaseInsensitiveObjectWrapper(json), parsedIdentifier).IsNone());

        EXPECT_EQ(identifier, parsedIdentifier);
    }
}

} // namespace aos::common::cloudprotocol
