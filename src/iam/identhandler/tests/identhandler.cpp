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
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/iam/identhandler/identmodules/fileidentifier/fileidentifier.hpp>

#include <common/utils/exception.hpp>
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
    }

    void TearDown() override { std::filesystem::remove_all(cFileIdentifierRoot); }

    crypto::DefaultCryptoProvider mCryptoProvider;
};

config::IdentifierConfig CreateFileIdentifierConfig()
{
    config::IdentifierConfig config;

    config.mPlugin = "fileidentifier";

    auto params = Poco::makeShared<Poco::JSON::Object>();

    if (std::ofstream f(cFileIdentifierRoot / "systemIDPath"); f.is_open()) {
        f << "SYSTEM-123456";

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

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(IdentHandlerTest, ModuleNotSet)
{
    auto identModule = InitializeIdentModule({}, mCryptoProvider);

    EXPECT_EQ(identModule, nullptr);
}

TEST_F(IdentHandlerTest, FileIdentifierModule)
{
    try {
        auto identModule = InitializeIdentModule(CreateFileIdentifierConfig(), mCryptoProvider);

        EXPECT_NE(identModule, nullptr);
        EXPECT_NE(dynamic_cast<FileIdentifier*>(identModule.get()), nullptr);
    } catch (const std::exception& e) {
        LOG_ERR() << common::utils::ToAosError(e);

        FAIL() << "Exception thrown";
    }
}

TEST_F(IdentHandlerTest, VisModule)
{
    try {
        config::IdentifierConfig config;

        config.mPlugin = "visidentifier";

        auto identModule = InitializeIdentModule(config, mCryptoProvider);

        EXPECT_NE(identModule, nullptr);
        EXPECT_NE(dynamic_cast<aos::iam::visidentifier::VISIdentifier*>(identModule.get()), nullptr);
    } catch (const std::exception& e) {
        LOG_ERR() << common::utils::ToAosError(e);

        FAIL() << "Exception thrown";
    }
}

} // namespace aos::iam::identhandler
