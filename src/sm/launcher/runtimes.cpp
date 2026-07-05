/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include "runtimes.hpp"

#ifdef WITH_RUNTIME_BOOT
#include "runtimes/boot/boot.hpp"
#endif

#ifdef WITH_RUNTIME_ROOTFS
#include "runtimes/rootfs/rootfs.hpp"
#endif

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

// Which parameters are used depends on which WITH_RUNTIME_* runtimes are compiled in below.
Error Runtimes::Init(const Config&                             config,
    [[maybe_unused]] iamclient::CurrentNodeInfoProviderItf&    currentNodeInfoProvider,
    [[maybe_unused]] imagemanager::ItemInfoProviderItf&        itemInfoProvider,
    [[maybe_unused]] networkmanager::NetworkManagerItf&        networkManager,
    [[maybe_unused]] iamclient::PermHandlerItf&                permHandler,
    [[maybe_unused]] resourcemanager::ResourceInfoProviderItf& resourceInfoProvider,
    [[maybe_unused]] oci::OCISpecItf& ociSpec, [[maybe_unused]] InstanceStatusReceiverItf& statusReceiver,
    [[maybe_unused]] sm::utils::SystemdConnItf&       systemdConn,
    [[maybe_unused]] launcher::InstanceIDProviderItf& instanceIDProvider)
{
    LOG_DBG() << "Init runtimes" << Log::Field("numRuntimes", config.mRuntimes.size());

    for (const auto& runtimeConfig : config.mRuntimes) {
        LOG_DBG() << "Init runtime" << Log::Field("plugin", runtimeConfig.mPlugin.c_str())
                  << Log::Field("type", runtimeConfig.mType.c_str());

#ifdef WITH_RUNTIME_CONTAINER
        if (runtimeConfig.mPlugin == cRuntimeContainer) {
            auto runtime = std::make_unique<ContainerRuntime>();

            if (auto err = runtime->Init(runtimeConfig, currentNodeInfoProvider, itemInfoProvider, networkManager,
                    permHandler, resourceInfoProvider, ociSpec, statusReceiver, instanceIDProvider);
                !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mRuntimes.emplace_back(std::move(runtime));
        } else
#endif
#ifdef WITH_RUNTIME_BOOT
            if (runtimeConfig.mPlugin == cRuntimeBoot) {
            auto runtime = std::make_unique<BootRuntime>();

            if (auto err = runtime->Init(
                    runtimeConfig, currentNodeInfoProvider, itemInfoProvider, ociSpec, statusReceiver, systemdConn);
                !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mRuntimes.emplace_back(std::move(runtime));
        } else
#endif
#ifdef WITH_RUNTIME_ROOTFS
            if (runtimeConfig.mPlugin == cRuntimeRootfs) {
            auto runtime = std::make_unique<RootfsRuntime>();

            if (auto err = runtime->Init(
                    runtimeConfig, currentNodeInfoProvider, itemInfoProvider, ociSpec, statusReceiver, systemdConn);
                !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mRuntimes.emplace_back(std::move(runtime));
        } else
#endif
        {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eNotSupported, "runtime is not supported"));
        }
    }

    return ErrorEnum::eNone;
}

Error Runtimes::GetRuntimes(Array<RuntimeItf*>& runtimes) const
{
    for (const auto& runtime : mRuntimes) {
        if (auto err = runtimes.EmplaceBack(runtime.get()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

ContainerRuntime* Runtimes::GetContainerRuntime() const
{
#ifdef WITH_RUNTIME_CONTAINER
    for (const auto& runtime : mRuntimes) {
        if (auto containerRuntime = dynamic_cast<ContainerRuntime*>(runtime.get()); containerRuntime != nullptr) {
            return containerRuntime;
        }
    }
#endif

    return nullptr;
}

} // namespace aos::sm::launcher
