/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <Poco/JSON/Object.h>

#include <gtest/gtest.h>

#include "config/config.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class ConfigTest : public Test {
public:
    ConfigTest()
    {
        std::ofstream file(mFileName);
        file << R"({
            "IAMPublicServerURL": "localhost:8090",
            "IAMProtectedServerURL": "localhost:8089",
            "CACert": "/etc/ssl/certs/rootCA.crt",
            "CertStorage": "/var/aos/crypt/iam/",
            "NodeID": "NodeID",
            "NodeType": "NodeType",
            "WorkingDir": "/var/aos/iamanager",
            "FinishProvisioningCmdArgs": [
                "/var/aos/finish.sh"
            ],
            "DiskEncryptionCmdArgs": [
                "/bin/sh",
                "/var/aos/encrypt.sh"
            ],
            "EnablePermissionsHandler": true,
            "RemoteIams": [
                {
                    "NodeID": "Node1",
                    "URL": "remotehost1:8089"
                },
                {
                    "NodeID": "Node2",
                    "URL": "remotehost2:8089",
                    "RequestTimeout": "100s"
                }
            ],
            "CertModules":[{
                "ID": "id1",
                "Plugin": "test1",
                "Algorithm": "rsa",
                "MaxItems": 1,
                "ExtendedKeyUsage": ["clientAuth"],
                "AlternativeNames": ["host1"],
                "SkipValidation": true,
                "Params": {
                    "Param1" :"value1",
                    "Param2" : 2
                }
            }, {
                "ID": "id2",
                "Plugin": "test2",
                "Algorithm": "ecc",
                "MaxItems": 2,
                "ExtendedKeyUsage": ["serverAuth"],
                "AlternativeNames": ["host2"],
                "SkipValidation": false,
                "Params": {
                    "Param1" :"value1",
                    "Param2" : 2
                }
            }, {
                "ID": "id3",
                "Plugin": "test3",
                "Algorithm": "rsa",
                "MaxItems": 3,
                "ExtendedKeyUsage": ["clientAuth", "serverAuth"],
                "AlternativeNames": ["host3"],
                "Disabled": true,
                "Params": {
                    "Param1" :"value1",
                    "Param2" : 2
                }
            }],
            "Identifier": {
                "Plugin": "testIdentifier",
                "Params": {
                    "Param1": "Value1",
                    "Param2": "Value2"
                }
            }
        })";
    }

    ~ConfigTest() { std::remove(mFileName.c_str()); }

protected:
    std::string mFileName = "config_test.json";
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ConfigTest, ParseConfig)
{
    auto [config, error] = ParseConfig(mFileName);
    ASSERT_EQ(error, aos::ErrorEnum::eNone);

    EXPECT_EQ(config.mIAMPublicServerURL, "localhost:8090");
    EXPECT_EQ(config.mIAMProtectedServerURL, "localhost:8089");
    EXPECT_EQ(config.mCACert, "/etc/ssl/certs/rootCA.crt");
    EXPECT_EQ(config.mCertStorage, "/var/aos/crypt/iam/");
    EXPECT_EQ(config.mNodeID, "NodeID");
    EXPECT_EQ(config.mNodeType, "NodeType");
    EXPECT_EQ(config.mWorkingDir, "/var/aos/iamanager");
    EXPECT_EQ(config.mEnablePermissionsHandler, true);

    EXPECT_EQ(config.mFinishProvisioningCmdArgs, std::vector<std::string> {"/var/aos/finish.sh"});
    EXPECT_EQ(config.mDiskEncryptionCmdArgs, std::vector<std::string>({"/bin/sh", "/var/aos/encrypt.sh"}));

    EXPECT_EQ(config.mRemoteIAMs.size(), 2);

    EXPECT_EQ(config.mRemoteIAMs[0].mNodeID, "Node1");
    EXPECT_EQ(config.mRemoteIAMs[0].mURL, "remotehost1:8089");
    EXPECT_EQ(config.mRemoteIAMs[1].mNodeID, "Node2");
    EXPECT_EQ(config.mRemoteIAMs[1].mURL, "remotehost2:8089");
    EXPECT_EQ(config.mRemoteIAMs[1].mRequestTimeout, std::chrono::seconds(100));

    EXPECT_EQ(config.mCertModules.size(), 3);

    EXPECT_EQ(config.mCertModules[0].mID, "id1");
    EXPECT_EQ(config.mCertModules[0].mPlugin, "test1");
    EXPECT_EQ(config.mCertModules[0].mAlgorithm, "rsa");
    EXPECT_EQ(config.mCertModules[0].mMaxItems, 1);
    EXPECT_EQ(config.mCertModules[0].mExtendedKeyUsage, std::vector<std::string> {"clientAuth"});
    EXPECT_EQ(config.mCertModules[0].mAlternativeNames, std::vector<std::string> {"host1"});
    EXPECT_EQ(config.mCertModules[0].mSkipValidation, true);
    auto params = config.mCertModules[0].mParams.extract<Poco::JSON::Object::Ptr>();
    EXPECT_EQ(params->get("Param1").convert<std::string>(), "value1");
    EXPECT_EQ(params->get("Param2").convert<std::string>(), "2");

    EXPECT_EQ(config.mCertModules[1].mID, "id2");
    EXPECT_EQ(config.mCertModules[1].mPlugin, "test2");
    EXPECT_EQ(config.mCertModules[1].mAlgorithm, "ecc");
    EXPECT_EQ(config.mCertModules[1].mMaxItems, 2);
    EXPECT_EQ(config.mCertModules[1].mExtendedKeyUsage, std::vector<std::string> {"serverAuth"});
    EXPECT_EQ(config.mCertModules[1].mAlternativeNames, std::vector<std::string> {"host2"});
    EXPECT_EQ(config.mCertModules[1].mSkipValidation, false);
    params = config.mCertModules[1].mParams.extract<Poco::JSON::Object::Ptr>();
    EXPECT_EQ(params->get("Param1").convert<std::string>(), "value1");
    EXPECT_EQ(params->get("Param2").convert<std::string>(), "2");

    EXPECT_EQ(config.mCertModules[2].mID, "id3");
    EXPECT_EQ(config.mCertModules[2].mPlugin, "test3");
    EXPECT_EQ(config.mCertModules[2].mAlgorithm, "rsa");
    EXPECT_EQ(config.mCertModules[2].mMaxItems, 3);
    EXPECT_EQ(config.mCertModules[2].mExtendedKeyUsage, std::vector<std::string>({"clientAuth", "serverAuth"}));
    EXPECT_EQ(config.mCertModules[2].mAlternativeNames, std::vector<std::string> {"host3"});
    EXPECT_EQ(config.mCertModules[2].mDisabled, true);
    params = config.mCertModules[2].mParams.extract<Poco::JSON::Object::Ptr>();
    EXPECT_EQ(params->get("Param1").convert<std::string>(), "value1");
    EXPECT_EQ(params->get("Param2").convert<std::string>(), "2");

    EXPECT_EQ(config.mIdentifier.mPlugin, "testIdentifier");

    params = config.mIdentifier.mParams.extract<Poco::JSON::Object::Ptr>();

    EXPECT_EQ(params->get("Param1").convert<std::string>(), "Value1");
    EXPECT_EQ(params->get("Param2").convert<std::string>(), "Value2");
}

TEST_F(ConfigTest, ParsePKCS11ModuleParams)
{
    Poco::JSON::Object::Ptr params = new Poco::JSON::Object();
    params->set("library", "/usr/lib/pkcs11.so");
    params->set("slotIndex", 2);
    params->set("tokenLabel", "token");
    params->set("userPINPath", "/var/aos/pin");
    params->set("modulePathInURL", true);

    auto [pkcs11Params, error] = ParsePKCS11ModuleParams(params);
    ASSERT_EQ(error, aos::ErrorEnum::eNone);

    EXPECT_EQ(pkcs11Params.mUserPINPath, "/var/aos/pin");
    EXPECT_EQ(pkcs11Params.mModulePathInURL, true);
    EXPECT_EQ(pkcs11Params.mLibrary, "/usr/lib/pkcs11.so");
    EXPECT_EQ(pkcs11Params.mSlotIndex.value(), 2);
    EXPECT_EQ(pkcs11Params.mTokenLabel, "token");
    EXPECT_EQ(pkcs11Params.mSlotID, std::nullopt);
}

TEST_F(ConfigTest, ParseVISIdentifierModuleParams)
{
    Poco::JSON::Object::Ptr params = new Poco::JSON::Object();
    params->set("visServer", "localhost:8089");
    params->set("caCertFile", "/etc/ssl/certs/rootCA.crt");
    params->set("webSocketTimeout", 100);

    auto [visParams, error] = ParseVISIdentifierModuleParams(params);
    ASSERT_EQ(error, aos::ErrorEnum::eNone);

    EXPECT_EQ(visParams.mVISServer, "localhost:8089");
    EXPECT_EQ(visParams.mCaCertFile, "/etc/ssl/certs/rootCA.crt");
    EXPECT_EQ(visParams.mWebSocketTimeout, 100);
}
