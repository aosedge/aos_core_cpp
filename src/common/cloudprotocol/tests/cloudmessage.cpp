/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/cloudmessage.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

template <class T>
std::unique_ptr<aos::cloudprotocol::MessageVariant> CreateMessage()
{
    auto message = std::make_unique<aos::cloudprotocol::MessageVariant>();

    message->SetValue<T>();

    return message;
}

template <typename T>
class VariantComparator : public StaticVisitor<bool> {
public:
    VariantComparator(const T& expected)
        : mExpected(expected)
    {
    }

    Res Visit(const T& variantVal) const { return variantVal == mExpected; }

    template <typename U>
    Res Visit(const U&) const
    {
        return false;
    }

private:
    const T& mExpected;
};

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/
class CloudProtocolCloudMessage : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolCloudMessage, MessageHeader)
{
    // version is missing
    {
        auto json = Poco::makeShared<Poco::JSON::Object>();
        json->set("systemID", "system1");

        aos::cloudprotocol::MessageHeader parsedHeader;
        ASSERT_EQ(FromJSON(utils::CaseInsensitiveObjectWrapper(json), parsedHeader), ErrorEnum::eInvalidArgument);
    }

    // systemID is missing
    {
        auto json = Poco::makeShared<Poco::JSON::Object>();
        json->set("version", 1);

        aos::cloudprotocol::MessageHeader parsedHeader;
        ASSERT_EQ(FromJSON(utils::CaseInsensitiveObjectWrapper(json), parsedHeader), ErrorEnum::eInvalidArgument);
    }

    aos::cloudprotocol::MessageHeader header;
    header.mVersion  = 1;
    header.mSystemID = "system1";

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(header, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<int>("version", -1), 1);
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("systemID", ""), "system1");

    aos::cloudprotocol::MessageHeader parsedHeader;
    ASSERT_EQ(FromJSON(jsonWrapper, parsedHeader), ErrorEnum::eNone);

    ASSERT_EQ(header, parsedHeader);
}

TEST_F(CloudProtocolCloudMessage, CloudMessageFailsOnDataTagMissing)
{
    auto json = Poco::makeShared<Poco::JSON::Object>();
    json->set("header", Poco::makeShared<Poco::JSON::Object>());

    auto jsonStr = utils::Stringify(json);

    auto parsedMessage = std::make_unique<aos::cloudprotocol::CloudMessage>();
    ASSERT_EQ(FromJSON(jsonStr, *parsedMessage), ErrorEnum::eInvalidArgument);
}

TEST_F(CloudProtocolCloudMessage, CloudMessageFailsOnHeaderTagMissing)
{
    auto json = Poco::makeShared<Poco::JSON::Object>();
    json->set("data", Poco::makeShared<Poco::JSON::Object>());

    auto jsonStr = utils::Stringify(json);

    auto parsedMessage = std::make_unique<aos::cloudprotocol::CloudMessage>();
    ASSERT_EQ(FromJSON(jsonStr, *parsedMessage), ErrorEnum::eInvalidArgument);
}

TEST_F(CloudProtocolCloudMessage, CloudMessageFailsOnUnknownMessageType)
{
    auto json = Poco::makeShared<Poco::JSON::Object>();

    {
        auto header = Poco::makeShared<Poco::JSON::Object>();

        header->set("version", 1);
        header->set("systemID", "system1");

        json->set("header", header);
    }

    {
        auto data = Poco::makeShared<Poco::JSON::Object>();

        data->set("type", "unknownType");

        json->set("data", data);
    }

    auto jsonStr = utils::Stringify(json);

    auto parsedMessage = std::make_unique<aos::cloudprotocol::CloudMessage>();
    ASSERT_EQ(FromJSON(jsonStr, *parsedMessage), ErrorEnum::eNotFound);
}

TEST_F(CloudProtocolCloudMessage, ConvertVariant)
{
    std::array variants = {
        CreateMessage<aos::cloudprotocol::Alerts>(),
        CreateMessage<aos::cloudprotocol::DeprovisioningRequest>(),
        CreateMessage<aos::cloudprotocol::DeprovisioningResponse>(),
        CreateMessage<aos::cloudprotocol::DesiredStatus>(),
        CreateMessage<aos::cloudprotocol::FinishProvisioningRequest>(),
        CreateMessage<aos::cloudprotocol::FinishProvisioningResponse>(),
        CreateMessage<aos::cloudprotocol::InstallUnitCertsConfirmation>(),
        CreateMessage<aos::cloudprotocol::IssuedUnitCerts>(),
        CreateMessage<aos::cloudprotocol::IssueUnitCerts>(),
        CreateMessage<aos::cloudprotocol::Monitoring>(),
        CreateMessage<aos::cloudprotocol::NewState>(),
        CreateMessage<aos::cloudprotocol::OverrideEnvVarsRequest>(),
        CreateMessage<aos::cloudprotocol::OverrideEnvVarsStatuses>(),
        CreateMessage<aos::cloudprotocol::PushLog>(),
        CreateMessage<aos::cloudprotocol::RenewCertsNotification>(),
        CreateMessage<aos::cloudprotocol::RequestLog>(),
        CreateMessage<aos::cloudprotocol::StartProvisioningRequest>(),
        CreateMessage<aos::cloudprotocol::StartProvisioningResponse>(),
        CreateMessage<aos::cloudprotocol::StateAcceptance>(),
        CreateMessage<aos::cloudprotocol::StateRequest>(),
        CreateMessage<aos::cloudprotocol::UnitStatus>(),
        CreateMessage<aos::cloudprotocol::UpdateState>(),
    };

    for (size_t i = 0; i < variants.size(); ++i) {
        const auto& variant = variants[i];

        auto message = std::make_unique<aos::cloudprotocol::CloudMessage>();

        message->mHeader.mVersion  = 1;
        message->mHeader.mSystemID = "system1";

        message->mData = *variant;

        auto json = Poco::makeShared<Poco::JSON::Object>();
        EXPECT_EQ(ToJSON(*message, *json), ErrorEnum::eNone);

        auto parsedMessage = std::make_unique<aos::cloudprotocol::CloudMessage>();

        auto jsonStr = utils::Stringify(json);

        auto err = FromJSON(jsonStr, *parsedMessage);
        EXPECT_TRUE(err.IsNone()) << "caseNumber: " << i << ",json: " << jsonStr
                                  << ", error: " << tests::utils::ErrorToStr(err);

        EXPECT_TRUE(*parsedMessage == *message);
    }
}

} // namespace aos::common::cloudprotocol
