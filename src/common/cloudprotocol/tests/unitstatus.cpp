/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/unitstatus.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void SetArchInfo(const String& arch, const Optional<StaticString<cCPUVariantLen>>& variant, ArchInfo& archInfo)
{
    archInfo.mArchitecture = arch;
    archInfo.mVariant      = variant;
}

void SetOSInfo(const String& os, const Optional<StaticString<cVersionLen>>& version,
    const std::vector<StaticString<cOSFeatureLen>>& features, OSInfo& osInfo)
{
    osInfo.mOS      = os;
    osInfo.mVersion = version;

    for (const auto& feature : features) {
        auto err = osInfo.mFeatures.EmplaceBack(feature);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    }
}

void SetChecksum(const std::string& str, Array<uint8_t>& checksum)
{
    auto err = String(str.c_str()).HexToByteArray(checksum);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolUnitStatus : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolUnitStatus, UnitStatus)
{
    constexpr auto cJSON = R"({"messageType":"unitStatus","correlationId":"id","isDeltaInfo":false,"unitConfig":[)"
                           R"({"version":"0.0.1","state":"failed","errorInfo":{"aosCode":1,"exitCode":0,)"
                           R"("message":"error message"}},)"
                           R"({"version":"0.0.2","state":"installed"}]})";

    auto unitStatus            = std::make_unique<UnitStatus>();
    unitStatus->mCorrelationID = "id";

    unitStatus->mUnitConfig.EmplaceValue();

    unitStatus->mUnitConfig->EmplaceBack();
    unitStatus->mUnitConfig->Back().mVersion = "0.0.1";
    unitStatus->mUnitConfig->Back().mState   = UnitConfigStateEnum::eFailed;
    unitStatus->mUnitConfig->Back().mError   = Error(Error::Enum::eFailed, "error message");

    unitStatus->mUnitConfig->EmplaceBack();
    unitStatus->mUnitConfig->Back().mVersion = "0.0.2";
    unitStatus->mUnitConfig->Back().mState   = UnitConfigStateEnum::eInstalled;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*unitStatus, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolUnitStatus, Nodes)
{
    constexpr auto cJSON
        = R"({"messageType":"unitStatus","correlationId":"id","isDeltaInfo":false,"nodes":[)"
          R"({"identity":{"codename":"nodeID1","title":"title1"},"nodeGroupSubject":{"codename":"type1"},)"
          R"("maxDmips":10000,"physicalRam":8096,"totalRam":16384,"osInfo":{"os":"Linux",)"
          R"("version":"5.10","features":["feature1","feature2"]},"cpus":[{"modelName":)"
          R"("Intel Xeon","totalNumCores":8,"totalNumThreads":16,"archInfo":{"architecture":)"
          R"("x86_64","variant":"variant1"},"maxDmips":5000}],"atts":{"attr1":"value1",)"
          R"("attr2":"value2"},"partitions":[{"name":"part1","types":["type1","type2"],)"
          R"("totalSize":1073741824}],"runtimes":[{"identity":{"codename":"runtimeID1"},)"
          R"("runtimeType":"type1","archInfo":{"architecture":"x86_64","variant":"variant1"},)"
          R"("osInfo":{"os":"Linux","version":"5.10","features":["feature1","feature2"]},)"
          R"("maxDmips":2000,"allowedDmips":1000,"totalRam":4096,"allowedRam":2048,)"
          R"("maxInstances":10}],"resources":[{"name":"resourceID1","sharedCount":1},)"
          R"({"name":"resourceID2","sharedCount":2}],"state":"provisioned","isConnected":true},)"
          R"({"identity":{"codename":"nodeID2","title":"title2"},"nodeGroupSubject":{"codename":"type2"},)"
          R"("maxDmips":20000,"totalRam":8096,"osInfo":{"os":"Linux","version":"5.10",)"
          R"("features":["feature1","feature2"]},"state":"error","isConnected":false,)"
          R"("errorInfo":{"aosCode":1,"exitCode":0,"message":""}}]})";

    auto unitStatus            = std::make_unique<UnitStatus>();
    unitStatus->mCorrelationID = "id";

    unitStatus->mNodes.EmplaceValue();

    unitStatus->mNodes->EmplaceBack();
    unitStatus->mNodes->Back().mNodeID   = "nodeID1";
    unitStatus->mNodes->Back().mTitle    = "title1";
    unitStatus->mNodes->Back().mNodeType = "type1";
    unitStatus->mNodes->Back().mMaxDMIPS = 10000;
    unitStatus->mNodes->Back().mPhysicalRAM.EmplaceValue(8096);
    unitStatus->mNodes->Back().mTotalRAM = 16384;
    SetOSInfo("Linux", {"5.10"}, {"feature1", "feature2"}, unitStatus->mNodes->Back().mOSInfo);

    unitStatus->mNodes->Back().mCPUs.EmplaceBack();
    unitStatus->mNodes->Back().mCPUs.Back().mModelName  = "Intel Xeon";
    unitStatus->mNodes->Back().mCPUs.Back().mNumCores   = 8;
    unitStatus->mNodes->Back().mCPUs.Back().mNumThreads = 16;
    unitStatus->mNodes->Back().mCPUs.Back().mMaxDMIPS.EmplaceValue(5000);
    SetArchInfo("x86_64", {"variant1"}, unitStatus->mNodes->Back().mCPUs.Back().mArchInfo);

    unitStatus->mNodes->Back().mAttrs.EmplaceBack();
    unitStatus->mNodes->Back().mAttrs.Back().mName  = "attr1";
    unitStatus->mNodes->Back().mAttrs.Back().mValue = "value1";
    unitStatus->mNodes->Back().mAttrs.EmplaceBack();
    unitStatus->mNodes->Back().mAttrs.Back().mName  = "attr2";
    unitStatus->mNodes->Back().mAttrs.Back().mValue = "value2";

    unitStatus->mNodes->Back().mPartitions.EmplaceBack();
    unitStatus->mNodes->Back().mPartitions.Back().mName = "part1";
    unitStatus->mNodes->Back().mPartitions.Back().mTypes.EmplaceBack("type1");
    unitStatus->mNodes->Back().mPartitions.Back().mTypes.EmplaceBack("type2");
    unitStatus->mNodes->Back().mPartitions.Back().mTotalSize = 1024 * 1024 * 1024;

    unitStatus->mNodes->Back().mRuntimes.EmplaceBack();
    unitStatus->mNodes->Back().mRuntimes.Back().mRuntimeID   = "runtimeID1";
    unitStatus->mNodes->Back().mRuntimes.Back().mRuntimeType = "type1";
    SetArchInfo("x86_64", {"variant1"}, unitStatus->mNodes->Back().mRuntimes.Back().mArchInfo);
    SetOSInfo("Linux", {"5.10"}, {"feature1", "feature2"}, unitStatus->mNodes->Back().mRuntimes.Back().mOSInfo);
    unitStatus->mNodes->Back().mRuntimes.Back().mMaxDMIPS.EmplaceValue(2000);
    unitStatus->mNodes->Back().mRuntimes.Back().mAllowedDMIPS.EmplaceValue(1000);
    unitStatus->mNodes->Back().mRuntimes.Back().mTotalRAM.EmplaceValue(4096);
    unitStatus->mNodes->Back().mRuntimes.Back().mAllowedRAM.EmplaceValue(2048);
    unitStatus->mNodes->Back().mRuntimes.Back().mMaxInstances = 10;

    unitStatus->mNodes->Back().mResources.EmplaceBack();
    unitStatus->mNodes->Back().mResources.Back().mName        = "resourceID1";
    unitStatus->mNodes->Back().mResources.Back().mSharedCount = 1;

    unitStatus->mNodes->Back().mResources.EmplaceBack();
    unitStatus->mNodes->Back().mResources.Back().mName        = "resourceID2";
    unitStatus->mNodes->Back().mResources.Back().mSharedCount = 2;

    unitStatus->mNodes->Back().mState       = NodeStateEnum::eProvisioned;
    unitStatus->mNodes->Back().mIsConnected = true;

    unitStatus->mNodes->EmplaceBack();
    unitStatus->mNodes->Back().mNodeID   = "nodeID2";
    unitStatus->mNodes->Back().mTitle    = "title2";
    unitStatus->mNodes->Back().mNodeType = "type2";
    unitStatus->mNodes->Back().mMaxDMIPS = 20000;
    unitStatus->mNodes->Back().mTotalRAM = 8096;
    SetOSInfo("Linux", {"5.10"}, {"feature1", "feature2"}, unitStatus->mNodes->Back().mOSInfo);
    unitStatus->mNodes->Back().mState       = NodeStateEnum::eError;
    unitStatus->mNodes->Back().mError       = ErrorEnum::eFailed;
    unitStatus->mNodes->Back().mIsConnected = false;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*unitStatus, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolUnitStatus, Items)
{
    constexpr auto cJSON
        = R"({"messageType":"unitStatus","correlationId":"id","isDeltaInfo":false,"items":[)"
          R"({"item":{"id":"itemID1"},"version":"version1","state":"downloading"},)"
          R"({"item":{"id":"itemID2"},"version":"version1","state":"installed"},)"
          R"({"item":{"id":"itemID3"},"version":"version1","state":"failed","errorInfo":{"aosCode":1,"exitCode":0,"message":"test error"}}]})";

    auto unitStatus            = std::make_unique<UnitStatus>();
    unitStatus->mCorrelationID = "id";

    unitStatus->mUpdateItems.EmplaceValue();

    unitStatus->mUpdateItems->EmplaceBack();
    unitStatus->mUpdateItems->Back().mItemID  = "itemID1";
    unitStatus->mUpdateItems->Back().mVersion = "version1";
    unitStatus->mUpdateItems->Back().mState   = ItemStateEnum::eDownloading;

    unitStatus->mUpdateItems->EmplaceBack();
    unitStatus->mUpdateItems->Back().mItemID  = "itemID2";
    unitStatus->mUpdateItems->Back().mVersion = "version1";
    unitStatus->mUpdateItems->Back().mState   = ItemStateEnum::eInstalled;

    unitStatus->mUpdateItems->EmplaceBack();
    unitStatus->mUpdateItems->Back().mItemID  = "itemID3";
    unitStatus->mUpdateItems->Back().mVersion = "version1";
    unitStatus->mUpdateItems->Back().mState   = ItemStateEnum::eFailed;
    unitStatus->mUpdateItems->Back().mError   = Error(ErrorEnum::eFailed, "test error");

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*unitStatus, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolUnitStatus, Instances)
{
    constexpr auto cJSON
        = R"({"messageType":"unitStatus","correlationId":"id","isDeltaInfo":false,"instances":[)"
          R"({"item":{"id":"itemID1"},"subject":{"id":"subjectID1"},"version":"version1","instances":[)"
          R"({"node":{"codename":"nodeID1"},"runtime":{"codename":"runtimeID1"},"instance":1,"stateChecksum":"12345678","state":"active"},)"
          R"({"node":{"codename":"nodeID1"},"runtime":{"codename":"runtimeID1"},"instance":2,"state":"failed","errorInfo":{"aosCode":1,"exitCode":0,"message":""}}]},)"
          R"({"item":{"id":"itemID2"},"subject":{"id":"subjectID2"},"version":"version2","instances":[)"
          R"({"node":{"codename":"nodeID2"},"runtime":{"codename":"runtimeID2"},"instance":1,"state":"activating"}]}]})";

    auto unitStatus            = std::make_unique<UnitStatus>();
    unitStatus->mCorrelationID = "id";

    unitStatus->mInstances.EmplaceValue();

    unitStatus->mInstances->EmplaceBack();
    unitStatus->mInstances->Back().mItemID    = "itemID1";
    unitStatus->mInstances->Back().mSubjectID = "subjectID1";
    unitStatus->mInstances->Back().mVersion   = "version1";

    unitStatus->mInstances->Back().mInstances.EmplaceBack();
    unitStatus->mInstances->Back().mInstances.Back().mInstance  = 1;
    unitStatus->mInstances->Back().mInstances.Back().mNodeID    = "nodeID1";
    unitStatus->mInstances->Back().mInstances.Back().mRuntimeID = "runtimeID1";
    unitStatus->mInstances->Back().mInstances.Back().mState     = InstanceStateEnum::eActive;
    SetChecksum("12345678", unitStatus->mInstances->Back().mInstances.Back().mStateChecksum);

    unitStatus->mInstances->Back().mInstances.EmplaceBack();
    unitStatus->mInstances->Back().mInstances.Back().mInstance  = 2;
    unitStatus->mInstances->Back().mInstances.Back().mNodeID    = "nodeID1";
    unitStatus->mInstances->Back().mInstances.Back().mRuntimeID = "runtimeID1";
    unitStatus->mInstances->Back().mInstances.Back().mState     = InstanceStateEnum::eFailed;
    unitStatus->mInstances->Back().mInstances.Back().mError     = ErrorEnum::eFailed;

    unitStatus->mInstances->EmplaceBack();
    unitStatus->mInstances->Back().mItemID    = "itemID2";
    unitStatus->mInstances->Back().mSubjectID = "subjectID2";
    unitStatus->mInstances->Back().mVersion   = "version2";

    unitStatus->mInstances->Back().mInstances.EmplaceBack();
    unitStatus->mInstances->Back().mInstances.Back().mInstance  = 1;
    unitStatus->mInstances->Back().mInstances.Back().mNodeID    = "nodeID2";
    unitStatus->mInstances->Back().mInstances.Back().mRuntimeID = "runtimeID2";
    unitStatus->mInstances->Back().mInstances.Back().mState     = InstanceStateEnum::eActivating;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*unitStatus, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolUnitStatus, PreinstalledInstances)
{
    constexpr auto cJSON
        = R"({"messageType":"unitStatus","correlationId":"id","isDeltaInfo":false,"instances":[)"
          R"({"item":{"id":"itemID1"},"subject":{"id":"subjectID1"},"version":"version1","instances":[)"
          R"({"node":{"codename":"nodeID1"},"runtime":{"codename":"runtimeID1"},"instance":1,"stateChecksum":"12345678","state":"active"},)"
          R"({"node":{"codename":"nodeID1"},"runtime":{"codename":"runtimeID1"},"instance":2,"state":"failed","errorInfo":{"aosCode":1,"exitCode":0,"message":""}}]},)"
          R"({"item":{"codename":"itemID2"},"subject":{"codename":"subjectID2"},"version":"version2","instances":[)"
          R"({"node":{"codename":"nodeID2"},"runtime":{"codename":"runtimeID2"},"instance":1,"state":"activating"}]}]})";

    auto unitStatus            = std::make_unique<UnitStatus>();
    unitStatus->mCorrelationID = "id";

    unitStatus->mInstances.EmplaceValue();

    unitStatus->mInstances->EmplaceBack();
    unitStatus->mInstances->Back().mItemID    = "itemID1";
    unitStatus->mInstances->Back().mSubjectID = "subjectID1";
    unitStatus->mInstances->Back().mVersion   = "version1";

    unitStatus->mInstances->Back().mInstances.EmplaceBack();
    unitStatus->mInstances->Back().mInstances.Back().mInstance  = 1;
    unitStatus->mInstances->Back().mInstances.Back().mNodeID    = "nodeID1";
    unitStatus->mInstances->Back().mInstances.Back().mRuntimeID = "runtimeID1";
    unitStatus->mInstances->Back().mInstances.Back().mState     = InstanceStateEnum::eActive;
    SetChecksum("12345678", unitStatus->mInstances->Back().mInstances.Back().mStateChecksum);

    unitStatus->mInstances->Back().mInstances.EmplaceBack();
    unitStatus->mInstances->Back().mInstances.Back().mInstance  = 2;
    unitStatus->mInstances->Back().mInstances.Back().mNodeID    = "nodeID1";
    unitStatus->mInstances->Back().mInstances.Back().mRuntimeID = "runtimeID1";
    unitStatus->mInstances->Back().mInstances.Back().mState     = InstanceStateEnum::eFailed;
    unitStatus->mInstances->Back().mInstances.Back().mError     = ErrorEnum::eFailed;

    unitStatus->mInstances->EmplaceBack();
    unitStatus->mInstances->Back().mItemID       = "itemID2";
    unitStatus->mInstances->Back().mSubjectID    = "subjectID2";
    unitStatus->mInstances->Back().mVersion      = "version2";
    unitStatus->mInstances->Back().mPreinstalled = true;

    unitStatus->mInstances->Back().mInstances.EmplaceBack();
    unitStatus->mInstances->Back().mInstances.Back().mInstance  = 1;
    unitStatus->mInstances->Back().mInstances.Back().mNodeID    = "nodeID2";
    unitStatus->mInstances->Back().mInstances.Back().mRuntimeID = "runtimeID2";
    unitStatus->mInstances->Back().mInstances.Back().mState     = InstanceStateEnum::eActivating;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*unitStatus, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolUnitStatus, Subjects)
{
    constexpr auto cJSON = R"({"messageType":"unitStatus","correlationId":"id","isDeltaInfo":false,)"
                           R"("subjects":[{"codename":"subject1"},{"codename":"subject2"}]})";

    auto unitStatus            = std::make_unique<UnitStatus>();
    unitStatus->mCorrelationID = "id";

    unitStatus->mUnitSubjects.EmplaceValue();

    unitStatus->mUnitSubjects->EmplaceBack("subject1");
    unitStatus->mUnitSubjects->EmplaceBack("subject2");

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*unitStatus, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

} // namespace aos::common::cloudprotocol
