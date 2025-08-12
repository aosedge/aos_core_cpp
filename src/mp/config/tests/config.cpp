/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <mp/config/config.hpp>

namespace aos::mp::config {

namespace {

/***********************************************************************************************************************
 * Test utils
 **********************************************************************************************************************/

void CreateTempConfigFile(const std::string& filename, const std::string& content)
{
    std::ofstream ofs(filename, std::ios::binary);
    ofs.write(content.c_str(), content.size());
    ofs.close();
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tests::utils::InitLog();

        tempConfigFile = "temp_config.json";

        std::string content = R"({
            "CACert": "/etc/Root_CA.pem",
            "CertStorage": "sm",
            "WorkingDir": "/path/to/download",
            "ImageStoreDir": "/path/to/images",
            "IAMConfig": {
                "IAMPublicServerURL": "localhost:8090",
                "IAMMainPublicServerURL": "main:8090",
                "IAMMainProtectedServerURL": "main:8091",
                "CertStorage": "iam",
                "OpenPort": 8080,
                "SecurePort": 8081
            },
            "CMConfig": {
                "CMServerURL": "localhost:8095",
                "OpenPort": 8080,
                "SecurePort": 8081
            },
            "VChan": {
                "Domain": 1,
                "XSRXPath": "/path/to/rx",
                "XSTXPath": "/path/to/tx",
                "IAMCertStorage": "iam-certs",
                "SMCertStorage": "sm-certs"
            },
            "Downloader": {
                "DownloadDir": "/var/aos/workdirs/mp/downloads"
            }
        })";

        CreateTempConfigFile(tempConfigFile, content);
    }

    void TearDown() override { std::filesystem::remove(tempConfigFile); }

    std::string tempConfigFile;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ConfigTest, ParseConfig)
{
    auto result = ParseConfig(tempConfigFile);

    ASSERT_EQ(result.mError, ErrorEnum::eNone);
    const Config& config = result.mValue;

    EXPECT_EQ(config.mCACert, "/etc/Root_CA.pem");
    EXPECT_EQ(config.mCertStorage, "sm");
    EXPECT_EQ(config.mWorkingDir, "/path/to/download");
    EXPECT_EQ(config.mImageStoreDir, "/path/to/images");

    EXPECT_EQ(config.mIAMConfig.mIAMPublicServerURL, "localhost:8090");
    EXPECT_EQ(config.mIAMConfig.mIAMMainPublicServerURL, "main:8090");
    EXPECT_EQ(config.mIAMConfig.mIAMMainProtectedServerURL, "main:8091");
    EXPECT_EQ(config.mIAMConfig.mCertStorage, "iam");
    EXPECT_EQ(config.mIAMConfig.mOpenPort, 8080);
    EXPECT_EQ(config.mIAMConfig.mSecurePort, 8081);

    EXPECT_EQ(config.mCMConfig.mCMServerURL, "localhost:8095");
    EXPECT_EQ(config.mCMConfig.mOpenPort, 8080);
    EXPECT_EQ(config.mCMConfig.mSecurePort, 8081);

    EXPECT_EQ(config.mVChan.mXSRXPath, "/path/to/rx");
    EXPECT_EQ(config.mVChan.mXSTXPath, "/path/to/tx");
    EXPECT_EQ(config.mVChan.mIAMCertStorage, "iam-certs");
    EXPECT_EQ(config.mVChan.mSMCertStorage, "sm-certs");
    EXPECT_EQ(config.mVChan.mDomain, 1);

    EXPECT_EQ(config.mDownload.mDownloadDir, "/var/aos/workdirs/mp/downloads");
}

} // namespace aos::mp::config
