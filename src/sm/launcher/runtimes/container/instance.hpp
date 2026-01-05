/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_INSTANCE_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_INSTANCE_HPP_

#include <mutex>

#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/common/types/instance.hpp>
#include <core/sm/imagemanager/itf/iteminfoprovider.hpp>

#include "itf/filesystem.hpp"
#include "itf/runner.hpp"

#include "config.hpp"

namespace aos::sm::launcher {

/**
 * Launcher instance.
 */
class Instance {
public:
    /**
     * Constructor.
     *
     * @param instanceInfo instance info.
     * @param config container runtime config.
     * @param fileSystem file system interface.
     * @param runner runner interface.
     * @param itemInfoProvider item info provider.
     * @param ociSpec OCI spec interface.
     */
    Instance(const InstanceInfo& instance, const ContainerConfig& config, FileSystemItf& fileSystem, RunnerItf& runner,
        imagemanager::ItemInfoProviderItf& itemInfoProvider, oci::OCISpecItf& ociSpec);

    /**
     * Constructor.
     *
     * @param instanceID instance ID.
     * @param config container runtime config.
     * @param fileSystem file system interface.
     * @param runner runner interface.
     * @param itemInfoProvider item info provider.
     * @param ociSpec OCI spec interface.
     */
    Instance(const std::string& instanceID, const ContainerConfig& config, FileSystemItf& fileSystem, RunnerItf& runner,
        imagemanager::ItemInfoProviderItf& itemInfoProvider, oci::OCISpecItf& ociSpec);

    /**
     * Starts instance.
     *
     * @return Error
     */
    Error Start();

    /**
     * Stops instance.
     *
     * @return Error
     */
    Error Stop();

    /**
     * Returns instance ID.
     *
     * @return std::string.
     */
    std::string InstanceID() const { return mInstanceID; }

    /**
     * Returns instance status.
     *
     * @param[out] status instance status.
     */
    void GetStatus(InstanceStatus& status) const;

private:
    static constexpr auto cRuntimeConfigFile = "config.json";
    static constexpr auto cCgroupsPath       = "/system.slice/system-aos\\x2dservice.slice";

    void  GenerateInstanceID();
    Error LoadConfigs(oci::ImageConfig& imageConfig, oci::ServiceConfig& serviceConfig);
    Error CreateRuntimeConfig(const std::string& runtimeDir, const oci::ImageConfig& imageConfig,
        const oci::ServiceConfig& serviceConfig, oci::RuntimeConfig& runtimeConfig);

    InstanceInfo mInstanceInfo;
    std::string  mInstanceID;
    RunStatus    mRunStatus;

    const ContainerConfig&             mConfig;
    FileSystemItf&                     mFileSystem;
    RunnerItf&                         mRunner;
    imagemanager::ItemInfoProviderItf& mItemInfoProvider;
    oci::OCISpecItf&                   mOCISpec;

    mutable std::mutex mMutex;
};

} // namespace aos::sm::launcher

#endif
