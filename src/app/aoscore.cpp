/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utils/exception.hpp>

#include "config/config.hpp"
#include "logger/logmodule.hpp"

#include "version.hpp" // cppcheck-suppress missingInclude

#include "aoscore.hpp"

namespace aos::sm::app {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

void AosCore::Init(const std::string& configFile)
{
    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize logger", err);

    LOG_INF() << "Initialize SM: version = " << AOS_CORE_SM_VERSION;
    LOG_DBG() << "Aos core size: size=" << sizeof(AosCore);

    // Initialize Aos modules

    auto config = std::make_shared<config::Config>();

    Tie(*config, err) = config::ParseConfig(configFile.empty() ? cDefaultConfigFile : configFile);
    AOS_ERROR_CHECK_AND_THROW("can't parse config", err);

    // Initialize crypto provider

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize crypto provider", err);

    // Initialize cert loader

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW("can't initialize cert loader", err);

    // Initialize IAM client

    auto iamConfig = std::make_unique<common::iamclient::Config>();

    iamConfig->mCACert             = config->mCACert;
    iamConfig->mIAMPublicServerURL = config->mIAMPublicServerURL;

    err = mIAMClientPublic.Init(*iamConfig, mCertLoader, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW("can't initialize public IAM client", err);

    auto nodeInfo = std::make_shared<NodeInfo>();

    err = mIAMClientPublic.GetNodeInfo(*nodeInfo);
    AOS_ERROR_CHECK_AND_THROW("can't get node info", err);

    err = mIAMClientPermissions.Init(config->mIAMProtectedServerURL, config->mCertStorage, mIAMClientPublic);
    AOS_ERROR_CHECK_AND_THROW("can't initialize permissions IAM client", err);

    // Initialize host device manager

    err = mHostDeviceManager.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize host device manager", err);

    // Initialize resource manager

    err = mResourceManager.Init(
        mJSONProvider, mHostDeviceManager, nodeInfo->mNodeType, config->mNodeConfigFile.c_str());
    AOS_ERROR_CHECK_AND_THROW("can't initialize resource manager", err);

    // Initialize database

    err = mDatabase.Init(config->mWorkingDir, config->mMigration);
    AOS_ERROR_CHECK_AND_THROW("can't initialize database", err);

    // Initialize network manager

    err = mNetworkManager.Init(
        mDatabase, mCNI, mTrafficMonitor, mNamespaceManager, mNetworkInterfaceManager, config->mWorkingDir.c_str());
    AOS_ERROR_CHECK_AND_THROW("can't initialize network manager", err);

    // Initialize resource monitor

    err = mResourceMonitor.Init(mIAMClientPublic, mResourceUsageProvider, mSMClient, mSMClient);
    AOS_ERROR_CHECK_AND_THROW("can't initialize resource monitor", err);

    // Initialize image handler

    err = mImageHandler.Init(mCryptoProvider, mLayersSpaceAllocator, mDownloadSpaceAllocator, mOCISpec);
    AOS_ERROR_CHECK_AND_THROW("can't initialize image handler", err);

    // Initialize service manager

    auto serviceManagerConfig = std::make_shared<sm::servicemanager::Config>();

    serviceManagerConfig->mServicesDir = config->mServicesDir.c_str();
    serviceManagerConfig->mDownloadDir = config->mDownloadDir.c_str();
    serviceManagerConfig->mTTL         = config->mServiceTTL.count();

    err = mServiceManager.Init(*serviceManagerConfig, mOCISpec, mDownloader, mDatabase, mServicesSpaceAllocator,
        mDownloadSpaceAllocator, mImageHandler);
    AOS_ERROR_CHECK_AND_THROW("can't initialize service manager", err);

    // Initialize layer manager

    auto layerManagerConfig = std::make_shared<sm::layermanager::Config>();

    layerManagerConfig->mLayersDir   = config->mLayersDir.c_str();
    layerManagerConfig->mDownloadDir = config->mDownloadDir.c_str();
    layerManagerConfig->mTTL         = config->mLayerTTL.count();

    err = mLayerManager.Init(*layerManagerConfig, mLayersSpaceAllocator, mDownloadSpaceAllocator, mDatabase,
        mDownloader, mImageHandler, mOCISpec);
    AOS_ERROR_CHECK_AND_THROW("can't initialize layer manager", err);

    // Initialize launcher

    auto launcherConfig = std::make_shared<sm::launcher::Config>();

    launcherConfig->mWorkDir    = config->mWorkingDir.c_str();
    launcherConfig->mStorageDir = config->mStorageDir.c_str();
    launcherConfig->mStateDir   = config->mStateDir.c_str();

    for (const auto& bind : config->mHostBinds) {
        err = launcherConfig->mHostBinds.EmplaceBack(bind.c_str());
        AOS_ERROR_CHECK_AND_THROW("can't add host bind", err);
    }

    for (const auto& host : config->mHosts) {
        err = launcherConfig->mHosts.EmplaceBack(Host {host.mIP.c_str(), host.mHostname.c_str()});
        AOS_ERROR_CHECK_AND_THROW("can't add host", err);
    }

    err = mLauncher.Init(*launcherConfig, mIAMClientPublic, mServiceManager, mLayerManager, mResourceManager,
        mNetworkManager, mIAMClientPermissions, mRunner, mRuntime, mResourceMonitor, mOCISpec, mSMClient, mSMClient,
        mDatabase);
    AOS_ERROR_CHECK_AND_THROW("can't initialize launcher", err);

    // Initialize SM client

    err = mSMClient.Init(*config, mIAMClientPublic, mIAMClientPublic, mResourceManager, mNetworkManager, mLogProvider,
        mResourceMonitor, mLauncher);
    AOS_ERROR_CHECK_AND_THROW("can't initialize SM client", err);
}

void AosCore::Start()
{
    auto err = mSMClient.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start SM client", err);

    err = mLauncher.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start launcher", err);

    err = mLayerManager.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start layer manager", err);

    err = mNetworkManager.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start network manager", err);

    err = mResourceMonitor.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start resource monitor", err);

    err = mServiceManager.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start service manager", err);
}

void AosCore::Stop()
{
    auto err = mSMClient.Stop();
    AOS_ERROR_CHECK_AND_THROW("can't stop SM client", err);

    err = mLauncher.Stop();
    AOS_ERROR_CHECK_AND_THROW("can't stop launcher", err);

    err = mLayerManager.Stop();
    AOS_ERROR_CHECK_AND_THROW("can't stop layer manager", err);

    err = mNetworkManager.Stop();
    AOS_ERROR_CHECK_AND_THROW("can't stop network manager", err);

    err = mResourceMonitor.Stop();
    AOS_ERROR_CHECK_AND_THROW("can't stop resource monitor", err);

    err = mServiceManager.Stop();
    AOS_ERROR_CHECK_AND_THROW("can't stop service manager", err);
}

void AosCore::SetLogBackend(common::logger::Logger::Backend backend)
{
    mLogger.SetBackend(backend);
}

void AosCore::SetLogLevel(LogLevel level)
{
    mLogger.SetLogLevel(level);
}

} // namespace aos::sm::app
