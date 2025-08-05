/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/alerts.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateCoreAlert()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto coreAlert    = std::make_unique<aos::cloudprotocol::CoreAlert>();

    coreAlert->mNodeID.Assign("test_node");
    coreAlert->mCoreComponent = aos::cloudprotocol::CoreComponentEnum::eUpdateManager;
    coreAlert->mMessage.Assign("Test core alert message");

    alertVariant->SetValue(std::move(*coreAlert));

    return alertVariant;
}

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateDeviceAllocateAlert()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto alert        = std::make_unique<aos::cloudprotocol::DeviceAllocateAlert>();

    alert->mInstanceIdent = {"service_id", "subject_id", 1};
    alert->mNodeID.Assign("test_node");
    alert->mDevice.Assign("test_device");
    alert->mMessage.Assign("Test device allocation alert message");

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateDownloadAlert()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto alert        = std::make_unique<aos::cloudprotocol::DownloadAlert>();

    alert->mTargetType = aos::cloudprotocol::DownloadTargetEnum::eService;
    alert->mTargetID.Assign("test_target_id");
    alert->mVersion.Assign("1.0.0");
    alert->mMessage.Assign("Test download alert message");
    alert->mURL.Assign("http://example.com/download");
    alert->mDownloadedBytes.Assign("100");
    alert->mTotalBytes.Assign("1000");

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateInstanceQuotaAlert()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto alert        = std::make_unique<aos::cloudprotocol::InstanceQuotaAlert>();

    alert->mInstanceIdent = {"service_id", "subject_id", 1};
    alert->mParameter.Assign("test_parameter");
    alert->mValue = 42;

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateServiceInstanceAlert()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto alert        = std::make_unique<aos::cloudprotocol::ServiceInstanceAlert>();

    alert->mInstanceIdent = {"service_id", "subject_id", 1};
    alert->mServiceVersion.Assign("1.0.0");
    alert->mMessage.Assign("Test service instance alert message");

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateSystemAlert()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto alert        = std::make_unique<aos::cloudprotocol::SystemAlert>();

    alert->mNodeID.Assign("test_node");
    alert->mMessage.Assign("Test system alert message");

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateSystemQuotaAlert()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto alert        = std::make_unique<aos::cloudprotocol::SystemQuotaAlert>();

    alert->mNodeID.Assign("test_node");
    alert->mParameter.Assign("test_parameter");
    alert->mValue = 100;

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateResourceValidateAlertWithNoErrors()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto alert        = std::make_unique<aos::cloudprotocol::ResourceValidateAlert>();

    alert->mNodeID.Assign("test_node");
    alert->mName.Assign("test_resource");

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<aos::cloudprotocol::AlertVariant> CreateResourceValidateAlertWithErrors()
{
    auto alertVariant = std::make_unique<aos::cloudprotocol::AlertVariant>();
    auto alert        = std::make_unique<aos::cloudprotocol::ResourceValidateAlert>();

    alert->mNodeID.Assign("test_node");
    alert->mName.Assign("test_resource");

    alert->mErrors.EmplaceBack(Error(1, "Error 1"));
    alert->mErrors.EmplaceBack(ErrorEnum::eFailed);

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/
class CloudProtocolAlerts : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolAlerts, EmptyAlerts)
{
    auto alerts = std::make_unique<aos::cloudprotocol::Alerts>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*alerts, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "alerts");
    EXPECT_TRUE(wrapper.Has("items"));

    auto unparsedAlerts = std::make_unique<aos::cloudprotocol::Alerts>();

    err = FromJSON(wrapper, *unparsedAlerts);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CloudProtocolAlerts, AlertsArray)
{
    const std::array alertsArray = {
        CreateCoreAlert(),
        CreateDeviceAllocateAlert(),
        CreateDownloadAlert(),
        CreateInstanceQuotaAlert(),
        CreateServiceInstanceAlert(),
        CreateSystemAlert(),
        CreateSystemQuotaAlert(),
        CreateResourceValidateAlertWithNoErrors(),
        CreateResourceValidateAlertWithErrors(),
    };

    auto alerts = std::make_unique<aos::cloudprotocol::Alerts>();

    for (const auto& alert : alertsArray) {
        auto err = alerts->mItems.PushBack(*alert);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    }

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*alerts, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "alerts");
    EXPECT_TRUE(wrapper.Has("items"));

    auto unparsedAlerts = std::make_unique<aos::cloudprotocol::Alerts>();

    err = FromJSON(wrapper, *unparsedAlerts);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace aos::common::cloudprotocol
