/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <Poco/JSON/Object.h>

#include <gtest/gtest.h>

#include <cm/config/config.hpp>

using namespace testing;

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

namespace {

constexpr auto cFullTestConfigJSON = R"({
    "fcrypt" : {
        "CACert" : "CACert",
        "tpmDevice": "/dev/tpmrm0",
        "pkcs11Library": "/path/to/pkcs11/library"
    },
    "certStorage": "/var/aos/crypt/cm/",
    "storageDir" : "/var/aos/storage",
    "stateDir" : "/var/aos/state",
    "serviceDiscoveryUrl" : "www.aos.com",
    "iamProtectedServerUrl" : "localhost:8089",
    "iamPublicServerUrl" : "localhost:8090",
    "cmServerUrl":"localhost:8094",
    "fileServerUrl":"localhost:8080",
    "workingDir" : "workingDir",
    "unitConfigFile" : "/var/aos/aos_unit.cfg",
    "cloudResponseWaitTimeout": "3d",
    "monitoring": {
        "sendPeriod": "5m"
    },
    "nodeinfoprovider": {
        "smConnectionTimeout": "10m"
    },
    "alerts": {
        "sendPeriod": "13m"
    },
    "imageManager": {
        "installPath": "/path/to/install",
        "updateItemTtl": "30d",
        "downloadPath": "/path/to/download"
    },
    "migration" : {
        "migrationPath" : "/usr/share/aos_communicationmanager/migration",
        "mergedMigrationPath" : "/var/aos/communicationmanager/migration"
    },
    "dnsStoragePath": "/var/aos/dnsstorage",
    "dnsIp": "0.0.0.0:5353"
})";

constexpr auto cMinimalTestConfigJSON = R"({
    "fcrypt" : {
        "CACert" : "CACert",
        "tpmDevice": "/dev/tpmrm0",
        "pkcs11Library": "/path/to/pkcs11/library"
    },
    "workingDir" : "workingDir",
    "serviceDiscoveryUrl" : "www.aos.com",
    "iamProtectedServerUrl" : "localhost:8089",
    "iamPublicServerUrl" : "localhost:8090",
    "cmServerUrl":"localhost:8094"
})";

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CMConfigTest : public Test {
public:
    void SetUp() override
    {
        if (std::ofstream file(cConfigFileName); file.good()) {
            file << cFullTestConfigJSON;
        }

        if (std::ofstream file(cMinimalTestConfigFileName); file.good()) {
            file << cMinimalTestConfigJSON;
        }
    }

    void TearDown() override
    {
        std::remove(cConfigFileName);
        std::remove(cMinimalTestConfigFileName);
    }

protected:
    static constexpr auto cConfigFileName            = "aos_communicationmanager.json";
    static constexpr auto cMinimalTestConfigFileName = "aos_communicationmanager_minimal.json";
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CMConfigTest, ParseFullConfig)
{
    aos::cm::config::Config config;

    auto err = aos::cm::config::ParseConfig(cConfigFileName, config);

    ASSERT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_EQ(config.mCrypt.mTpmDevice, "/dev/tpmrm0");
    EXPECT_EQ(config.mCrypt.mCACert, "CACert");
    EXPECT_EQ(config.mCrypt.mPkcs11Library, "/path/to/pkcs11/library");

    EXPECT_EQ(config.mServiceDiscoveryURL, "www.aos.com");
    EXPECT_EQ(config.mStorageDir, "/var/aos/storage");
    EXPECT_EQ(config.mStateDir, "/var/aos/state");
    EXPECT_EQ(config.mWorkingDir, "workingDir");
    EXPECT_EQ(config.mUnitConfigFile, "/var/aos/aos_unit.cfg");
    EXPECT_EQ(config.mIAMProtectedServerURL, "localhost:8089");
    EXPECT_EQ(config.mIAMPublicServerURL, "localhost:8090");
    EXPECT_EQ(config.mFileServerURL, "localhost:8080");
    EXPECT_EQ(config.mCMServerURL, "localhost:8094");
    EXPECT_EQ(config.mCertStorage, "/var/aos/crypt/cm/");

    EXPECT_EQ(config.mCloudResponseWaitTimeout, aos::Time::cDay * 3);

    EXPECT_EQ(config.mMonitoring.mSendPeriod, aos::Time::cMinutes * 5);
    EXPECT_EQ(config.mNodeInfoProvider.mSMConnectionTimeout, aos::Time::cMinutes * 10);
    EXPECT_EQ(config.mAlerts.mSendPeriod, aos::Time::cMinutes * 13);

    EXPECT_STREQ(config.mImageManager.mInstallPath.CStr(), "/path/to/install");
    EXPECT_EQ(config.mImageManager.mUpdateItemTTL, aos::Time::cDay * 30);
    EXPECT_STREQ(config.mImageManager.mDownloadPath.CStr(), "/path/to/download");

    EXPECT_EQ(config.mMigration.mMigrationPath, "/usr/share/aos_communicationmanager/migration");
    EXPECT_EQ(config.mMigration.mMergedMigrationPath, "/var/aos/communicationmanager/migration");

    EXPECT_EQ(config.mDNSStoragePath, "/var/aos/dnsstorage");
    EXPECT_EQ(config.mDNSIP, "0.0.0.0:5353");
}

TEST_F(CMConfigTest, ParseMinimalConfigWithDefaults)
{
    aos::cm::config::Config config;

    auto err = aos::cm::config::ParseConfig(cMinimalTestConfigFileName, config);

    ASSERT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_EQ(config.mCrypt.mTpmDevice, "/dev/tpmrm0");
    EXPECT_EQ(config.mCrypt.mCACert, "CACert");
    EXPECT_EQ(config.mCrypt.mPkcs11Library, "/path/to/pkcs11/library");

    EXPECT_EQ(config.mServiceDiscoveryURL, "www.aos.com");
    EXPECT_EQ(config.mWorkingDir, "workingDir");
    EXPECT_EQ(config.mIAMProtectedServerURL, "localhost:8089");
    EXPECT_EQ(config.mIAMPublicServerURL, "localhost:8090");
    EXPECT_EQ(config.mCMServerURL, "localhost:8094");

    EXPECT_EQ(config.mCertStorage, "/var/aos/crypt/cm/");
    EXPECT_EQ(config.mStorageDir, (std::filesystem::path("workingDir") / "storages").string());
    EXPECT_EQ(config.mStateDir, (std::filesystem::path("workingDir") / "states").string());
    EXPECT_EQ(config.mUnitConfigFile, (std::filesystem::path("workingDir") / "aos_unit.cfg").string());

    EXPECT_EQ(config.mUnitStatusSendTimeout, aos::Time::cSeconds * 30);
    EXPECT_EQ(config.mCloudResponseWaitTimeout, aos::Time::cSeconds * 10);

    EXPECT_EQ(config.mMonitoring.mSendPeriod, aos::Time::cMinutes * 1);
    EXPECT_EQ(config.mNodeInfoProvider.mSMConnectionTimeout, aos::Time::cMinutes * 1);
    EXPECT_EQ(config.mAlerts.mSendPeriod, aos::Time::cSeconds * 10);

    EXPECT_STREQ(config.mImageManager.mInstallPath.CStr(), (std::filesystem::path("workingDir") / "install").c_str());
    EXPECT_STREQ(config.mImageManager.mDownloadPath.CStr(), (std::filesystem::path("workingDir") / "download").c_str());
    EXPECT_EQ(config.mImageManager.mUpdateItemTTL, aos::Time::cDay * 30);

    EXPECT_EQ(config.mMigration.mMigrationPath, "/usr/share/aos/communicationmanager/migration");
    EXPECT_EQ(config.mMigration.mMergedMigrationPath, (std::filesystem::path("workingDir") / "migration").string());
}
