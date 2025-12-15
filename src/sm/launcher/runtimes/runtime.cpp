/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>
#include <sm/launcher/runtimes/rootfs/rootfs.hpp>

#include "runtime.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Runtimes::Init(const Config& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
    ImageManagerItf& imageManager, oci::OCISpecItf& ociSpec, InstanceStatusReceiverItf& statusReceiver,
    sm::utils::SystemdConnItf& systemdConn)
{
    LOG_DBG() << "Initialize runtimes";

    mCurrentNodeInfoProvider = &currentNodeInfoProvider;
    mImageManager            = &imageManager;
    mOciSpec                 = &ociSpec;
    mStatusReceiver          = &statusReceiver;
    mSystemdConn             = &systemdConn;

    if (auto err = mRebooter.Init(*mSystemdConn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto& rootfsConfig = config.mRootfsConfig; rootfsConfig.has_value()) {
        if (auto err = InitRootfsRuntime(rootfsConfig.value()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

StaticArray<RuntimeItf*, cMaxNumNodeRuntimes> Runtimes::GetRuntimes() const
{
    StaticArray<RuntimeItf*, cMaxNumNodeRuntimes> runtimes;

    for (const auto& runtime : mRuntimes) {
        runtimes.EmplaceBack(runtime.get());
    }

    return runtimes;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error Runtimes::InitRootfsRuntime(const RootfsConfig& config)
{
    auto rootfs = std::make_unique<rootfs::RootfsRuntime>();

    if (auto err = mRootfsUpdateChecker.Init(config.mUpdateServicesToCheck, *mSystemdConn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = rootfs->Init(config, *mCurrentNodeInfoProvider, *mImageManager, *mOciSpec, *mStatusReceiver,
            mRootfsUpdateChecker, mRebooter);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return StoreRuntime(std::move(rootfs));
}

Error Runtimes::StoreRuntime(std::unique_ptr<RuntimeItf>&& runtime)
{
    if (mRuntimes.size() >= cMaxNumNodeRuntimes) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNoMemory, "maximum number of runtimes exceeded"));
    }

    mRuntimes.emplace_back(std::move(runtime));

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
