/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

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
        aos::cloudprotocol::InstanceFilter {{}, {}, {}},
        aos::cloudprotocol::InstanceFilter {{"service1"}, {}, {}},
        aos::cloudprotocol::InstanceFilter {{"service1"}, {"subject1"}, {}},
        aos::cloudprotocol::InstanceFilter {{"service1"}, {"subject1"}, 42},
    };

    for (const auto& filter : instanceFilters) {
        auto json = Poco::makeShared<Poco::JSON::Object>();

        EXPECT_EQ(ToJSON(filter, *json), ErrorEnum::eNone);

        aos::cloudprotocol::InstanceFilter parsedFilter;
        EXPECT_TRUE(FromJSON(utils::CaseInsensitiveObjectWrapper(json), parsedFilter).IsNone());

        EXPECT_EQ(filter, parsedFilter);
    }
}

} // namespace aos::common::cloudprotocol
