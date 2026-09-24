/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Object.h>
#include <gmock/gmock.h>

#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/common/tools/heapallocator.hpp>
#include <core/iam/identhandler/identmodules/fileidentifier/fileidentifier.hpp>
#include <core/iam/tests/mocks/certloadermock.hpp>

#include <common/utils/exception.hpp>
#include <iam/identhandler/certidentifier.hpp>
#include <iam/identhandler/identhandler.hpp>
#include <iam/identhandler/visidentifier/visidentifier.hpp>

using namespace testing;

namespace aos::iam::identhandler {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const std::filesystem::path cFileIdentifierRoot = "file_identifier_test";

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class IdentHandlerTest : public testing::Test {
protected:
    void SetUp() override
    {
        tests::utils::InitLog();

        std::filesystem::create_directory(cFileIdentifierRoot);

        ASSERT_TRUE(mCryptoProvider.Init(mAllocator).IsNone());
    }

    void TearDown() override { std::filesystem::remove_all(cFileIdentifierRoot); }

    HeapAllocator                 mAllocator;
    crypto::DefaultCryptoProvider mCryptoProvider;
    iamclient::CertProviderMock   mCertProvider;
    crypto::CertLoaderMock        mCertLoader;
};

config::IdentifierConfig CreateFileIdentifierConfig()
{
    config::IdentifierConfig config;

    config.mPlugin = "fileidentifier";

    auto params = Poco::makeShared<Poco::JSON::Object>();

    if (std::ofstream f(cFileIdentifierRoot / "systemIDPath"); f.is_open()) {
        f << "system-id";

        params->set("systemIDPath", (cFileIdentifierRoot / "systemIDPath").string());
    }

    if (std::ofstream f(cFileIdentifierRoot / "unitModelPath"); f.is_open()) {
        f << "unitModel;1.0.0";

        params->set("unitModelPath", (cFileIdentifierRoot / "unitModelPath").string());
    }

    params->set("subjectsPath", (cFileIdentifierRoot / "subjectsPath").string());

    config.mParams = params;

    return config;
}

config::IdentifierConfig CreateCertIdentifierConfig()
{
    config::IdentifierConfig config;

    config.mPlugin = "certidentifier";

    auto params = Poco::makeShared<Poco::JSON::Object>();

    if (std::ofstream f(cFileIdentifierRoot / "unitModelPath"); f.is_open()) {
        f << "unitModel;1.0.0";

        params->set("unitModelPath", (cFileIdentifierRoot / "unitModelPath").string());
    }

    params->set("subjectsPath", (cFileIdentifierRoot / "subjectsPath").string());

    config.mParams = params;

    return config;
}

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(IdentHandlerTest, ModuleNotSet)
{
    try {
        auto identModule = InitializeIdentModule({}, mCryptoProvider, mCertProvider, mCertLoader, mAllocator);

        EXPECT_EQ(identModule, nullptr);
    } catch (const std::exception& e) {
        LOG_ERR() << common::utils::ToAosError(e);

        FAIL() << "Exception thrown";
    }
}

TEST_F(IdentHandlerTest, FileIdentifierModule)
{
    try {
        auto identModule = InitializeIdentModule(
            CreateFileIdentifierConfig(), mCryptoProvider, mCertProvider, mCertLoader, mAllocator);

        EXPECT_NE(identModule, nullptr);
        EXPECT_NE(dynamic_cast<FileIdentifier*>(identModule.get()), nullptr);
    } catch (const std::exception& e) {
        LOG_ERR() << common::utils::ToAosError(e);

        FAIL() << "Exception thrown";
    }
}

TEST_F(IdentHandlerTest, CertIdentifierModule)
{
    try {
        auto identModule = InitializeIdentModule(
            CreateCertIdentifierConfig(), mCryptoProvider, mCertProvider, mCertLoader, mAllocator);

        EXPECT_NE(identModule, nullptr);
        EXPECT_NE(dynamic_cast<CertIdentifier*>(identModule.get()), nullptr);
    } catch (const std::exception& e) {
        LOG_ERR() << common::utils::ToAosError(e);

        FAIL() << "Exception thrown";
    }
}

TEST_F(IdentHandlerTest, VisModule)
{
    try {
        GTEST_SKIP() << "VISIdentifier is disabled due to application crash on shutdown.";

        config::IdentifierConfig config;

        config.mPlugin = "visidentifier";

        auto params = Poco::makeShared<Poco::JSON::Object>();

        params->set("visServer", "ws://localhost:8081");
        params->set("vinVISPath", "Attribute.Vehicle.VehicleIdentification.VIN");
        params->set("unitModelPath", "Attribute.Vehicle.MyModel");
        params->set("subjectsPath", "Attribute.Vehicle.VehicleIdentification.Subjects");

        config.mParams = params;

        auto identModule = InitializeIdentModule(config, mCryptoProvider, mCertProvider, mCertLoader, mAllocator);

        EXPECT_NE(identModule, nullptr);
        EXPECT_NE(dynamic_cast<visidentifier::VISIdentifier*>(identModule.get()), nullptr);
    } catch (const std::exception& e) {
        LOG_ERR() << common::utils::ToAosError(e);

        FAIL() << "Exception thrown";
    }
}

} // namespace aos::iam::identhandler
