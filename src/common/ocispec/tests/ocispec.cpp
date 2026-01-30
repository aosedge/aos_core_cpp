/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Object.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/common/tools/fs.hpp>

#include <common/ocispec/ocispec.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::jsonprovider {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cTestBaseDir       = "ocispec_test_dir";
const auto     cImageIndexPath    = fs::JoinPath(cTestBaseDir, "image_index.json");
constexpr auto cImageIndex        = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.index.v1+json",
    "manifests": [
        {
            "mediaType": "application/vnd.oci.image.manifest.v1+json",
            "digest": "sha256:129abeb509f55870ec19f24eba0caecccee3f0e055c467e1df8513bdcddc746f",
            "size": 1018,
            "platform": {
                "architecture": "amd64",
                "variant": "6",
                "os": "linux",
                "os.version": "6.0.8",
                "os.features": [
                    "feature1",
                    "feature2"
                ]
            }
        }
    ]
}
)";
const auto     cImageManifestPath = fs::JoinPath(cTestBaseDir, "image_manifest.json");
constexpr auto cImageManifest     = R"({
    "schemaVersion": 2,
    "config": {
        "mediaType": "application/vnd.oci.image.config.v1+json",
        "digest": "sha256:a9fd89f4f021b5cd92fc993506886c243f024d4e4d863bc4939114c05c0b5f60",
        "size": 288
    },
    "aosService": {
        "mediaType": "application/vnd.aos.service.config.v1+json",
        "digest": "sha256:7bcbb9f29c1dd8e1d8a61eccdcf7eeeb3ec6072effdf6723707b5f4ead062e9c",
        "size": 322
    },
    "layers": [
        {
            "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip",
            "digest": "sha256:129abeb509f55870ec19f24eba0caecccee3f0e055c467e1df8513bdcddc746f",
            "size": 1018
        }
    ]
}
)";
const auto     cImageConfigPath   = fs::JoinPath(cTestBaseDir, "image_config.json");
constexpr auto cImageConfig       = R"(
{
    "architecture": "x86_64",
    "author": "gtest",
    "created": "2024-12-31T23:59:59Z",
    "os": "Linux",
    "osVersion": "6.0.8",
    "variant": "6",
    "config": {
        "exposedPorts": {
            "8080/tcp": {},
            "53/udp": {}
        },
        "cmd": [
            "test-cmd",
            "arg1",
            "arg2"
        ],
        "entrypoint": [
            "test-entrypoint",
            "arg1",
            "arg2"
        ],
        "env": [
            "env0",
            "env1",
            "env2",
            "env3",
            "env4",
            "env5"
        ],
        "workingDir": "/test-working-dir"
    },
    "rootfs": {
        "type": "layers",
        "diff_ids": [
            "sha256:129abeb509f55870ec19f24eba0caecccee3f0e055c467e1df8513bdcddc746f"
        ]
    }
}
)";
const auto     cItemConfigPath    = fs::JoinPath(cTestBaseDir, "item_config.json");
constexpr auto cItemConfig        = R"(
{
    "created": "2024-12-31T23:59:59Z",
    "author": "Aos cloud",
    "architecture": "x86",
    "balancingPolicy": "disabled",
    "hostname": "test-hostname",
    "runtimes": [
        "crun",
        "runc"
    ],
    "runParameters": {
        "startInterval": "PT1M",
        "startBurst": 0,
        "restartInterval": "PT5M"
    },
    "offlineTTL": "P1DT3H",
    "quotas": {
        "cpuLimit": 100,
        "ramLimit": 200,
        "storageLimit": 300,
        "stateLimit": 400,
        "tmpLimit": 500,
        "uploadSpeed": 600,
        "downloadSpeed": 700,
        "noFileLimit": 800,
        "pidsLimit": 900
    },
    "alertRules": {
        "ram": {
            "minTimeout": "PT1M",
            "minThreshold": 10,
            "maxThreshold": 20
        },
        "cpu": {
            "minTimeout": "PT2M",
            "minThreshold": 15,
            "maxThreshold": 25
        },
        "storage": {
            "name": "storage-name",
            "minTimeout": "PT3M",
            "minThreshold": 20,
            "maxThreshold": 30
        },
        "upload": {
            "minTimeout": "PT4M",
            "minThreshold": 250,
            "maxThreshold": 350
        },
        "download": {
            "minTimeout": "PT5M",
            "minThreshold": 300,
            "maxThreshold": 400
        }
    },
    "sysctl": {
        "key1": "value1",
        "key2": "value2"
    },
    "config": {
        "Entrypoint": [
            "python3"
        ],
        "Cmd": [
            "-u",
            "main.py"
        ],
        "WorkingDir": "/"
    },
    "allowedConnections": {
        "9931560c-be75-4f60-9abf-08297d905332/8087-8088/tcp": {},
        "9931560c-be75-4f60-9abf-08297d905332/1515/udp": {}
    },
    "resources": [
        "resource1",
        "resource2",
        "resource3"
    ],
    "permissions": [
        {
            "name": "name1",
            "permissions": [
                {
                    "function": "function1.1",
                    "permissions": "permissions1.1"
                },
                {
                    "function": "function1.2",
                    "permissions": "permissions1.2"
                }
            ]
        },
        {
            "name": "name2",
            "permissions": [
                {
                    "function": "function2.1",
                    "permissions": "permissions2.1"
                },
                {
                    "function": "function2.2",
                    "permissions": "permissions2.2"
                }
            ]
        }
    ]
}
)";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

aos::oci::LinuxDevice CreateLinuxDevice()
{
    return aos::oci::LinuxDevice {"/dev/device1", "rwm", 1, 2, {1}, {2}, {3}};
}

aos::oci::LinuxResources CreateLinuxResources()
{
    aos::oci::LinuxResources res;

    res.mDevices.EmplaceBack("device1", "rwm", false);

    res.mMemory.SetValue({1, 2, 3, 4, 5, 6, true, true, true});
    res.mCPU.SetValue({10, 11, 12, 13, 14, 15, StaticString<aos::oci::cMaxParamLen>("cpu0"),
        StaticString<aos::oci::cMaxParamLen>("mem0"), 16});
    res.mPids.SetValue({20});

    return res;
}

std::unique_ptr<aos::oci::RuntimeConfig> CreateRuntimeConfig()
{
    auto res = std::make_unique<aos::oci::RuntimeConfig>();

    aos::oci::CreateExampleRuntimeConfig(*res);

    aos::oci::Linux lnx;
    lnx.mResources.EmplaceValue(CreateLinuxResources());
    lnx.mDevices.EmplaceBack(CreateLinuxDevice());

    res->mLinux.SetValue(lnx);

    return res;
}

Poco::JSON::Object::Ptr ToJSON(const aos::RunParameters& params)
{
    Poco::JSON::Object object {Poco::JSON_PRESERVE_KEY_ORDER};

    if (params.mStartInterval.HasValue()) {
        object.set("startInterval", params.mStartInterval->ToISO8601String().CStr());
    }

    if (params.mStartBurst.HasValue()) {
        object.set("startBurst", *params.mStartBurst);
    }

    if (params.mRestartInterval.HasValue()) {
        object.set("restartInterval", params.mRestartInterval->ToISO8601String().CStr());
    }

    auto runParams = Poco::makeShared<Poco::JSON::Object>(object);

    runParams->set("runParameters", object);

    return runParams;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class OCISpecTest : public Test {
public:
    void SetUp() override
    {
        tests::utils::InitLog();

        fs::ClearDir(cTestBaseDir);

        fs::WriteStringToFile(cImageIndexPath, cImageIndex, S_IRUSR | S_IWUSR);
        fs::WriteStringToFile(cImageManifestPath, cImageManifest, S_IRUSR | S_IWUSR);
        fs::WriteStringToFile(cImageConfigPath, cImageConfig, S_IRUSR | S_IWUSR);
        fs::WriteStringToFile(cItemConfigPath, cItemConfig, S_IRUSR | S_IWUSR);
    }

    oci::OCISpec mOCISpec;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(OCISpecTest, LoadAndSaveImageIndex)
{
    auto lhsImageIndex = std::make_unique<aos::oci::ImageIndex>();
    auto rhsImageIndex = std::make_unique<aos::oci::ImageIndex>();

    const auto cSavePath = fs::JoinPath(cTestBaseDir, "image-index-save.json");

    auto err = mOCISpec.LoadImageIndex(cImageIndexPath, *lhsImageIndex);

    ASSERT_TRUE(err.IsNone()) << "LoadImageIndex failed: " << tests::utils::ErrorToStr(err);
    ASSERT_TRUE(mOCISpec.SaveImageIndex(cSavePath, *lhsImageIndex).IsNone());
    ASSERT_TRUE(mOCISpec.LoadImageIndex(cSavePath, *rhsImageIndex).IsNone());

    ASSERT_EQ(*lhsImageIndex, *rhsImageIndex);
}

TEST_F(OCISpecTest, LoadAndSaveImageManifest)
{
    auto lhsManifest = std::make_unique<aos::oci::ImageManifest>();
    auto rhsManifest = std::make_unique<aos::oci::ImageManifest>();

    const auto cSavePath = fs::JoinPath(cTestBaseDir, "image-manifest-save.json");

    ASSERT_TRUE(mOCISpec.LoadImageManifest(cImageManifestPath, *lhsManifest).IsNone());
    ASSERT_TRUE(mOCISpec.SaveImageManifest(cSavePath, *lhsManifest).IsNone());
    ASSERT_TRUE(mOCISpec.LoadImageManifest(cSavePath, *rhsManifest).IsNone());

    ASSERT_EQ(*lhsManifest, *rhsManifest);
}

TEST_F(OCISpecTest, LoadAndSaveImageConfig)
{
    auto lhsImageConfig = std::make_unique<aos::oci::ImageConfig>();
    auto rhsImageConfig = std::make_unique<aos::oci::ImageConfig>();

    const auto cSavePath = fs::JoinPath(cTestBaseDir, "image-config-save.json");

    ASSERT_TRUE(mOCISpec.LoadImageConfig(cImageConfigPath, *lhsImageConfig).IsNone());
    ASSERT_TRUE(mOCISpec.SaveImageConfig(cSavePath, *lhsImageConfig).IsNone());
    ASSERT_TRUE(mOCISpec.LoadImageConfig(cSavePath, *rhsImageConfig).IsNone());

    ASSERT_EQ(*lhsImageConfig, *rhsImageConfig);
}

TEST_F(OCISpecTest, LoadAndSaveRuntimeConfig)
{
    auto lhsRuntimeConfig = CreateRuntimeConfig();
    auto rhsRuntimeConfig = std::make_unique<aos::oci::RuntimeConfig>();

    const auto cSavePath = fs::JoinPath(cTestBaseDir, "runtime-config-save.json");

    ASSERT_TRUE(mOCISpec.SaveRuntimeConfig(cSavePath, *lhsRuntimeConfig).IsNone());
    ASSERT_TRUE(mOCISpec.LoadRuntimeConfig(cSavePath, *rhsRuntimeConfig).IsNone());

    ASSERT_EQ(*lhsRuntimeConfig, *rhsRuntimeConfig);
}

TEST_F(OCISpecTest, LoadAndSaveItemConfig)
{
    auto lhsItemConfig = std::make_unique<aos::oci::ItemConfig>();
    auto rhsItemConfig = std::make_unique<aos::oci::ItemConfig>();

    const auto cSavePath = fs::JoinPath(cTestBaseDir, "item-config-save.json");

    ASSERT_TRUE(mOCISpec.LoadItemConfig(cItemConfigPath, *lhsItemConfig).IsNone());
    ASSERT_TRUE(mOCISpec.SaveItemConfig(cSavePath, *lhsItemConfig).IsNone());
    ASSERT_TRUE(mOCISpec.LoadItemConfig(cSavePath, *rhsItemConfig).IsNone());
    ASSERT_EQ(*lhsItemConfig, *rhsItemConfig);
}

TEST_F(OCISpecTest, ServiceConfigFromFileRunParams)
{
    const std::vector<aos::RunParameters> runParams = {
        {{0}, {}, {}},
        {{}, {0}, {}},
        {{}, {}, {0}},
        {{}, {}, {}},
        {{1 * aos::Time::cSeconds}, {1 * aos::Time::cSeconds}, {1}},
    };

    for (size_t i = 0; i < runParams.size(); ++i) {
        LOG_DBG() << "Running test case #" << i;

        auto configPath = fs::JoinPath(cTestBaseDir, "run-params-config-");
        configPath.Append(std::to_string(i).c_str()).Append(".json");

        EXPECT_EQ(common::utils::WriteJsonToFile(ToJSON(runParams[i]), configPath.CStr()), ErrorEnum::eNone);

        auto expectedItemConfig            = std::make_unique<aos::oci::ItemConfig>();
        expectedItemConfig->mRunParameters = runParams[i];
        auto parsedItemConfig              = std::make_unique<aos::oci::ItemConfig>();

        ASSERT_EQ(mOCISpec.LoadItemConfig(configPath, *parsedItemConfig), ErrorEnum::eNone);
        ASSERT_EQ(expectedItemConfig->mRunParameters, parsedItemConfig->mRunParameters);
    }
}

} // namespace aos::common::jsonprovider
