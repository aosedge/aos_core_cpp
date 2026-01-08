/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include "runtimes.hpp"
#include "runtimes/boot/boot.hpp"
#include "runtimes/container/container.hpp"
#include "runtimes/rootfs/rootfs.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Runtimes::Init(const Config& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
    imagemanager::ItemInfoProviderItf& itemInfoProvider, oci::OCISpecItf& ociSpec,
    InstanceStatusReceiverItf& statusReceiver, sm::utils::SystemdConnItf& systemdConn)
{
    LOG_DBG() << "Init runtimes" << Log::Field("numRuntimes", config.mRuntimes.size());

    for (const auto& runtimeConfig : config.mRuntimes) {
        LOG_DBG() << "Init runtime" << Log::Field("plugin", runtimeConfig.mPlugin.c_str())
                  << Log::Field("type", runtimeConfig.mType.c_str());

        if (runtimeConfig.mPlugin == cRuntimeContainer) {
            auto runtime = std::make_unique<ContainerRuntime>();

            if (auto err = runtime->Init(runtimeConfig, currentNodeInfoProvider); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mRuntimes.emplace_back(std::move(runtime));
        } else if (runtimeConfig.mPlugin == cRuntimeBoot) {
            auto runtime = std::make_unique<BootRuntime>();

            if (auto err = runtime->Init(runtimeConfig, currentNodeInfoProvider); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mRuntimes.emplace_back(std::move(runtime));

        } else if (runtimeConfig.mPlugin == cRuntimeRootfs) {
            auto runtime = std::make_unique<RootfsRuntime>();

            if (auto err = runtime->Init(
                    runtimeConfig, currentNodeInfoProvider, itemInfoProvider, ociSpec, statusReceiver, systemdConn);
                !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mRuntimes.emplace_back(std::move(runtime));
        } else {
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

} // namespace aos::sm::launcher
