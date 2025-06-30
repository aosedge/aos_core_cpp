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
	"workingDir" : "workingDir",
	"imageStoreDir": "imagestoreDir",
	"componentsDir": "componentDir",
	"serviceTtlDays": "720h",
	"layerTtlDays": "720h",
	"unitConfigFile" : "/var/aos/aos_unit.cfg",
	"downloader": {
		"downloadDir": "/path/to/download",
		"maxConcurrentDownloads": 10,
		"retryDelay": "10s",
		"maxRetryDelay": "30s",
		"downloadPartLimit": 57
	},
	"monitoring": {
		"monitorConfig": {
			"pollPeriod": "1s"
		},
		"sendPeriod": "5m",
		"maxMessageSize": 1024,
		"maxOfflineMessages": 25
	},
	"alerts": {		
		"sendPeriod": "20s",
		"maxMessageSize": 1024,
		"maxOfflineMessages": 32,
		"journalAlerts": {
			"filter": ["test", "regexp"]
		}
	},
	"migration" : {
        "migrationPath" : "/usr/share/aos_communicationmanager/migration",
        "mergedMigrationPath" : "/var/aos/communicationmanager/migration"
    },
    "smController" : {
        "fileServerUrl" : "localhost:8094",
        "cmServerUrl" : "localhost:8093",
        "nodesConnectionTimeout" : "100s",
        "updateTtl" : "30h"
    },
	"umController": {
		"fileServerUrl" : "localhost:8092",
		"cmServerUrl" : "localhost:8091",
		"updateTtl" : "100h"
	}
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
	"cmServerUrl":"localhost:8094",
	"monitoring": {
		"monitorConfig": {
			"pollPeriod": "1s"
		}
	},
	"alerts": {		
		"journalAlerts": {
			"filter": ["test", "regexp"]
		}
	},
	"smController" : {"fileServerUrl" : "localhost:8094", "cmServerUrl" : "localhost:8093"},
	"umController": {
		"fileServerUrl" : "localhost:8092",
		"cmServerUrl" : "localhost:8091"
	}
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
    EXPECT_EQ(config.mImageStoreDir, "imagestoreDir");
    EXPECT_EQ(config.mStorageDir, "/var/aos/storage");
    EXPECT_EQ(config.mStateDir, "/var/aos/state");
    EXPECT_EQ(config.mWorkingDir, "workingDir");
    EXPECT_EQ(config.mUnitConfigFile, "/var/aos/aos_unit.cfg");
    EXPECT_EQ(config.mIAMProtectedServerURL, "localhost:8089");
    EXPECT_EQ(config.mIAMPublicServerURL, "localhost:8090");
    EXPECT_EQ(config.mCMServerURL, "localhost:8094");
    EXPECT_EQ(config.mCertStorage, "/var/aos/crypt/cm/");
    EXPECT_EQ(config.mComponentsDir, "componentDir");

    EXPECT_EQ(config.mServiceTTL, aos::Time::cHours * 24 * 30);
    EXPECT_EQ(config.mLayerTTL, aos::Time::cHours * 24 * 30);

    EXPECT_EQ(config.mDownloader.mDownloadDir, "/path/to/download");
    EXPECT_EQ(config.mDownloader.mMaxConcurrentDownloads, 10);
    EXPECT_EQ(config.mDownloader.mRetryDelay, aos::Time::cSeconds * 10);
    EXPECT_EQ(config.mDownloader.mMaxRetryDelay, aos::Time::cSeconds * 30);
    EXPECT_EQ(config.mDownloader.mDownloadPartLimit, 57);

    EXPECT_EQ(config.mMonitoring.mSendPeriod, aos::Time::cMinutes * 5);
    EXPECT_EQ(config.mMonitoring.mMaxOfflineMessages, 25);
    EXPECT_EQ(config.mMonitoring.mMaxMessageSize, 1024);

    EXPECT_EQ(config.mAlerts.mSendPeriod, aos::Time::cSeconds * 20);
    EXPECT_EQ(config.mAlerts.mMaxMessageSize, 1024);
    EXPECT_EQ(config.mAlerts.mMaxOfflineMessages, 32);

    std::vector<std::string> expectedFilter = {"test", "regexp"};
    EXPECT_EQ(config.mAlerts.mJournalAlerts.mFilter, expectedFilter);

    EXPECT_EQ(config.mUMController.mFileServerURL, "localhost:8092");
    EXPECT_EQ(config.mUMController.mCMServerURL, "localhost:8091");
    EXPECT_EQ(config.mUMController.mUpdateTTL, aos::Time::cHours * 100);

    EXPECT_EQ(config.mSMController.mFileServerURL, "localhost:8094");
    EXPECT_EQ(config.mSMController.mCMServerURL, "localhost:8093");
    EXPECT_EQ(config.mSMController.mNodesConnectionTimeout, aos::Time::cSeconds * 100);
    EXPECT_EQ(config.mSMController.mUpdateTTL, aos::Time::cHours * 30);

    EXPECT_EQ(config.mMigration.mMigrationPath, "/usr/share/aos_communicationmanager/migration");
    EXPECT_EQ(config.mMigration.mMergedMigrationPath, "/var/aos/communicationmanager/migration");
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
    EXPECT_EQ(config.mImageStoreDir, (std::filesystem::path("workingDir") / "imagestore").string());
    EXPECT_EQ(config.mComponentsDir, (std::filesystem::path("workingDir") / "components").string());
    EXPECT_EQ(config.mUnitConfigFile, (std::filesystem::path("workingDir") / "aos_unit.cfg").string());

    EXPECT_EQ(config.mServiceTTL, aos::Time::cHours * 24 * 30);
    EXPECT_EQ(config.mLayerTTL, aos::Time::cHours * 24 * 30);
    EXPECT_EQ(config.mUnitStatusSendTimeout, aos::Time::cSeconds * 30);

    EXPECT_EQ(config.mDownloader.mDownloadDir, (std::filesystem::path("workingDir") / "download").string());
    EXPECT_EQ(config.mDownloader.mMaxConcurrentDownloads, 4);
    EXPECT_EQ(config.mDownloader.mRetryDelay, aos::Time::cMinutes * 1);
    EXPECT_EQ(config.mDownloader.mMaxRetryDelay, aos::Time::cMinutes * 30);
    EXPECT_EQ(config.mDownloader.mDownloadPartLimit, 100);

    EXPECT_EQ(config.mMonitoring.mMaxOfflineMessages, 16);
    EXPECT_EQ(config.mMonitoring.mSendPeriod, aos::Time::cMinutes * 1);
    EXPECT_EQ(config.mMonitoring.mMaxMessageSize, 65536);

    EXPECT_EQ(config.mAlerts.mSendPeriod, aos::Time::cSeconds * 10);
    EXPECT_EQ(config.mAlerts.mMaxMessageSize, 65536);
    EXPECT_EQ(config.mAlerts.mMaxOfflineMessages, 25);

    std::vector<std::string> expectedFilter = {"test", "regexp"};
    EXPECT_EQ(config.mAlerts.mJournalAlerts.mFilter, expectedFilter);

    EXPECT_EQ(config.mUMController.mFileServerURL, "localhost:8092");
    EXPECT_EQ(config.mUMController.mCMServerURL, "localhost:8091");
    EXPECT_EQ(config.mUMController.mUpdateTTL, aos::Time::cHours * 720);

    EXPECT_EQ(config.mSMController.mFileServerURL, "localhost:8094");
    EXPECT_EQ(config.mSMController.mCMServerURL, "localhost:8093");
    EXPECT_EQ(config.mSMController.mNodesConnectionTimeout, aos::Time::cMinutes * 10);
    EXPECT_EQ(config.mSMController.mUpdateTTL, aos::Time::cHours * 720);

    EXPECT_EQ(config.mMigration.mMigrationPath, "/usr/share/aos/communicationmanager/migration");
    EXPECT_EQ(config.mMigration.mMergedMigrationPath, (std::filesystem::path("workingDir") / "migration").string());
}
