/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_INSTANCE_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_INSTANCE_HPP_

#include <mutex>

#include <core/common/iamclient/itf/permhandler.hpp>
#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/common/types/instance.hpp>
#include <core/sm/imagemanager/itf/iteminfoprovider.hpp>
#include <core/sm/networkmanager/itf/networkmanager.hpp>
#include <core/sm/resourcemanager/itf/resourceinfoprovider.hpp>

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
     * @param nodeInfo current node info.
     * @param fileSystem file system interface.
     * @param runner runner interface.
     * @param itemInfoProvider item info provider.
     * @param networkManager network manager.
     * @param permHandler permission handler.
     * @param resourceInfoProvider resource info provider.
     * @param ociSpec OCI spec interface.
     */
    Instance(const InstanceInfo& instance, const ContainerConfig& config, const NodeInfo& nodeInfo,
        FileSystemItf& fileSystem, RunnerItf& runner, imagemanager::ItemInfoProviderItf& itemInfoProvider,
        networkmanager::NetworkManagerItf& networkManager, iamclient::PermHandlerItf& permHandler,
        resourcemanager::ResourceInfoProviderItf& resourceInfoProvider, oci::OCISpecItf& ociSpec);

    /**
     * Constructor.
     *
     * @param instanceID instance ID.
     * @param config container runtime config.
     * @param nodeInfo current node info.
     * @param fileSystem file system interface.
     * @param runner runner interface.
     * @param itemInfoProvider item info provider.
     * @param networkManager network manager.
     * @param permHandler permission handler.
     * @param resourceInfoProvider resource info provider.
     * @param ociSpec OCI spec interface.
     */
    Instance(const std::string& instanceID, const ContainerConfig& config, const NodeInfo& nodeInfo,
        FileSystemItf& fileSystem, RunnerItf& runner, imagemanager::ItemInfoProviderItf& itemInfoProvider,
        networkmanager::NetworkManagerItf& networkManager, iamclient::PermHandlerItf& permHandler,
        resourcemanager::ResourceInfoProviderItf& resourceInfoProvider, oci::OCISpecItf& ociSpec);

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

    /**
     * Updates run status.
     *
     * @param runStatus run status.
     */
    void UpdateRunStatus(const RunStatus& runStatus);

    /**
     * Returns run status.
     *
     * @return RunStatus.
     */
    RunStatus GetRunStatus() const
    {
        std::lock_guard lock {mMutex};

        return mRunStatus;
    }

private:
    static constexpr auto cRuntimeConfigFile = "config.json";
    static constexpr auto cRootFSDir         = "rootfs";
    static constexpr auto cMountPointsDir    = "mounts";
    static constexpr auto cCgroupsPath       = "/system.slice/system-aos\\x2dservice.slice";

    static constexpr auto cEnvAosItemID        = "AOS_ITEM_ID";
    static constexpr auto cEnvAosSubjectID     = "AOS_SUBJECT_ID";
    static constexpr auto cEnvAosInstanceIndex = "AOS_INSTANCE_INDEX";
    static constexpr auto cEnvAosInstanceID    = "AOS_INSTANCE_ID";
    static constexpr auto cEnvAosSecret        = "AOS_SECRET";

    static constexpr auto cDefaultCPUPeriod = 100000;
    static constexpr auto cMinCPUQuota      = 1000;

    static constexpr auto cInstanceStateFile  = "/state.dat";
    static constexpr auto cInstanceStorageDir = "/storage";

    void   GenerateInstanceID();
    Error  LoadConfigs(oci::ImageConfig& imageConfig, oci::ServiceConfig& serviceConfig);
    Error  CreateRuntimeConfig(const std::string& runtimeDir, const oci::ImageConfig& imageConfig,
         const oci::ServiceConfig& serviceConfig, oci::RuntimeConfig& runtimeConfig);
    Error  BindHostDirs(oci::RuntimeConfig& runtimeConfig);
    Error  CreateAosEnvVars(oci::RuntimeConfig& runtimeConfig);
    Error  ApplyImageConfig(const oci::ImageConfig& imageConfig, oci::RuntimeConfig& runtimeConfig);
    Error  ApplyServiceConfig(const oci::ServiceConfig& serviceConfig, oci::RuntimeConfig& runtimeConfig);
    size_t GetNumCPUCores() const;
    Error  AddResources(const Array<StaticString<cResourceNameLen>>& resources, oci::RuntimeConfig& runtimeConfig);
    Error  AddDevices(const Array<StaticString<cDeviceNameLen>>& devices, oci::RuntimeConfig& runtimeConfig);
    Error  ApplyStateStorage(oci::RuntimeConfig& runtimeConfig);
    Error  OverrideEnvVars(oci::RuntimeConfig& runtimeConfig);
    Error  PrepareStateStorage();
    Error  PrepareRootFS(
         const std::string& runtimeDir, const oci::ImageConfig& imageConfig, const oci::RuntimeConfig& runtimeConfig);
    Error SetupNetwork(const std::string& runtimeDir, const oci::ServiceConfig& serviceConfig);
    Error AddNetworkHostsFromResource(const std::string& resource, std::vector<Host>& hosts);

    InstanceInfo mInstanceInfo;
    std::string  mInstanceID;
    RunStatus    mRunStatus;

    const ContainerConfig&                    mConfig;
    const NodeInfo&                           mNodeInfo;
    FileSystemItf&                            mFileSystem;
    RunnerItf&                                mRunner;
    imagemanager::ItemInfoProviderItf&        mItemInfoProvider;
    networkmanager::NetworkManagerItf&        mNetworkManager;
    iamclient::PermHandlerItf&                mPermHandler;
    resourcemanager::ResourceInfoProviderItf& mResourceInfoProvider;
    oci::OCISpecItf&                          mOCISpec;

    mutable std::mutex mMutex;

    bool mPermissionsRegistered {};
};

} // namespace aos::sm::launcher

#endif
