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

std::unique_ptr<AlertVariant> CreateCoreAlert()
{
    auto alertVariant = std::make_unique<AlertVariant>();
    auto coreAlert    = std::make_unique<CoreAlert>();

    coreAlert->mTimestamp = Time::Unix(0);
    coreAlert->mNodeID.Assign("test_node");
    coreAlert->mCoreComponent = CoreComponentEnum::eCM;
    coreAlert->mMessage.Assign("Test core alert message");

    alertVariant->SetValue(std::move(*coreAlert));

    return alertVariant;
}

std::unique_ptr<AlertVariant> CreateResourceAllocateAlert()
{
    auto alertVariant = std::make_unique<AlertVariant>();
    auto alert        = std::make_unique<ResourceAllocateAlert>();

    alert->mTimestamp = Time::Unix(0);
    alert->mItemID    = "itemID";
    alert->mSubjectID = "subjectID";
    alert->mInstance  = 1;

    alert->mNodeID.Assign("test_node");
    alert->mResource.Assign("test_resource");
    alert->mMessage.Assign("Test resource allocation alert message");

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<AlertVariant> CreateDownloadAlert(const Optional<String>& reason = {}, const Error error = {})
{
    auto alertVariant = std::make_unique<AlertVariant>();
    auto alert        = std::make_unique<DownloadAlert>();

    alert->mTimestamp = Time::Unix(0);
    alert->mDigest    = "testDigest";
    alert->mURL.Assign("http://example.com/download");
    alert->mDownloadedBytes = 100;
    alert->mTotalBytes      = 1000;
    alert->mState           = DownloadStateEnum::eStarted;

    if (reason.HasValue()) {
        alert->mReason.SetValue(reason.GetValue());
    }

    alert->mError = error;

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<AlertVariant> CreateInstanceQuotaAlert()
{
    auto alertVariant = std::make_unique<AlertVariant>();
    auto alert        = std::make_unique<InstanceQuotaAlert>();

    alert->mTimestamp = Time::Unix(0);
    alert->mItemID    = "itemID";
    alert->mSubjectID = "subjectID";
    alert->mInstance  = 1;

    alert->mParameter.Assign("test_parameter");
    alert->mValue = 42;

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<AlertVariant> CreateInstanceAlert()
{
    auto alertVariant = std::make_unique<AlertVariant>();
    auto alert        = std::make_unique<InstanceAlert>();

    alert->mTimestamp = Time::Unix(0);
    alert->mItemID    = "itemID";
    alert->mSubjectID = "subjectID";
    alert->mInstance  = 1;

    alert->mVersion.Assign("1.0.0");
    alert->mMessage.Assign("Test service instance alert message");

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<AlertVariant> CreateSystemAlert()
{
    auto alertVariant = std::make_unique<AlertVariant>();
    auto alert        = std::make_unique<SystemAlert>();

    alert->mTimestamp = Time::Unix(0);
    alert->mNodeID.Assign("test_node");
    alert->mMessage.Assign("Test system alert message");

    alertVariant->SetValue(std::move(*alert));

    return alertVariant;
}

std::unique_ptr<AlertVariant> CreateSystemQuotaAlert()
{
    auto alertVariant = std::make_unique<AlertVariant>();
    auto alert        = std::make_unique<SystemQuotaAlert>();

    alert->mTimestamp = Time::Unix(0);
    alert->mNodeID.Assign("test_node");
    alert->mParameter.Assign("test_parameter");
    alert->mValue = 100;

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

TEST_F(CloudProtocolAlerts, AlertsArray)
{
    constexpr auto cJSON
        = R"({"messageType":"alerts","correlationId":"id","items":[)"
          R"({"timestamp":"1970-01-01T00:00:00Z","tag":"coreAlert","node":{"codename":"test_node"},)"
          R"("coreComponent":"CM","message":"Test core alert message"},)"
          R"({"timestamp":"1970-01-01T00:00:00Z","tag":"resourceAllocateAlert","item":{"id":"itemID"},)"
          R"("subject":{"id":"subjectID"},"instance":1,"node":{"codename":"test_node"},)"
          R"("deviceId":"test_resource","message":"Test resource allocation alert message"},)"
          R"({"timestamp":"1970-01-01T00:00:00Z","tag":"downloadProgressAlert","digest":"testDigest",)"
          R"("url":"http://example.com/download","downloadedBytes":100,"totalBytes":1000,)"
          R"("state":"started"},)"
          R"({"timestamp":"1970-01-01T00:00:00Z","tag":"downloadProgressAlert","digest":"testDigest",)"
          R"("url":"http://example.com/download","downloadedBytes":100,"totalBytes":1000,)"
          R"("state":"started","reason":"test_reason","errorInfo":{"aosCode":1,"exitCode":0,)"
          R"("message":"test_error"}},)"
          R"({"timestamp":"1970-01-01T00:00:00Z","tag":"instanceQuotaAlert","item":{"id":"itemID"},)"
          R"("subject":{"id":"subjectID"},"instance":1,"parameter":"test_parameter","value":42},)"
          R"({"timestamp":"1970-01-01T00:00:00Z","tag":"updateItemInstanceAlert","item":{"id":"itemID"},)"
          R"("subject":{"id":"subjectID"},"instance":1,"version":"1.0.0",)"
          R"("message":"Test service instance alert message"},)"
          R"({"timestamp":"1970-01-01T00:00:00Z","tag":"systemAlert","node":{"codename":"test_node"},)"
          R"("message":"Test system alert message"},)"
          R"({"timestamp":"1970-01-01T00:00:00Z","tag":"systemQuotaAlert","node":{"codename":"test_node"},)"
          R"("parameter":"test_parameter","value":100}]})";

    const std::array alertsArray = {
        CreateCoreAlert(),
        CreateResourceAllocateAlert(),
        CreateDownloadAlert(),
        CreateDownloadAlert({"test_reason"}, Error(Error::Enum::eFailed, "test_error")),
        CreateInstanceQuotaAlert(),
        CreateInstanceAlert(),
        CreateSystemAlert(),
        CreateSystemQuotaAlert(),
    };

    auto alerts = std::make_unique<Alerts>();
    alerts->mCorrelationID.Assign("id");

    for (const auto& alert : alertsArray) {
        auto err = alerts->mItems.PushBack(*alert);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    }

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*alerts, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

} // namespace aos::common::cloudprotocol
