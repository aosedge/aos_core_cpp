/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <gtest/gtest.h>

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/mocks/ocispecmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/sm/tests/stubs/instancestatusreceiver.hpp>

#include <sm/launcher/runtimes/rootfs/rootfs.hpp>
#include <sm/launcher/runtimes/runtime.hpp>
#include <sm/launcher/runtimes/utils/systemdrebooter.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

using namespace testing;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Mocks
 **********************************************************************************************************************/

/**
 * Image manager mock.
 */
class ImageManagerMock : public ImageManagerItf {
public:
    MOCK_METHOD(Error, GetBlobPath, (const String& digest, String& path), (const, override));
};

} // namespace

class RuntimeTests : public Test {
protected:
    void SetUp() override { tests::utils::InitLog(); }

    Config                                 mConfig;
    iamclient::CurrentNodeInfoProviderMock mCurrentNodeInfoProvider;
    ImageManagerMock                       mImageManager;
    oci::OCISpecMock                       mOCISpec;
    InstanceStatusReceiverStub             mStatusReceiver;
    sm::utils::SystemdConnMock             mSystemdConn;
    Runtimes                               mRuntimes;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RuntimeTests, InitNoRuntimes)
{
    auto err
        = Runtimes {}.Init(mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    const auto runtimes = Runtimes {}.GetRuntimes();
    EXPECT_TRUE(runtimes.IsEmpty());
}

TEST_F(RuntimeTests, InitWithRootfsRuntime)
{
    mConfig.mRootfsConfig.emplace();

    auto& rootfsConfig               = *mConfig.mRootfsConfig;
    rootfsConfig.mRuntimeType        = "rootfs";
    rootfsConfig.mRuntimeID          = "rootfsId";
    rootfsConfig.mCurrentVersionFile = "versionFile";

    if (std::ofstream versionFile("versionFile"); versionFile.is_open()) {
        versionFile << "1.0.0";
    } else {
        throw std::runtime_error("can't create version file");
    }

    auto err
        = mRuntimes.Init(mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    const auto runtimes = mRuntimes.GetRuntimes();

    ASSERT_EQ(runtimes.Size(), 1);
    ASSERT_NE(dynamic_cast<rootfs::RootfsRuntime*>(runtimes[0]), nullptr);

    auto runtimeInfo = std::make_unique<RuntimeInfo>();

    err = runtimes[0]->GetRuntimeInfo(*runtimeInfo);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(runtimeInfo->mRuntimeType.CStr(), "rootfs");
    EXPECT_STREQ(runtimeInfo->mRuntimeID.CStr(), "rootfsId");
}

} // namespace aos::sm::launcher
