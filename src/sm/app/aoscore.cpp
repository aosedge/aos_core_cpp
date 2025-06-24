/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/version/version.hpp>
#include <sm/config/config.hpp>
#include <sm/logger/logmodule.hpp>

#include "aoscore.hpp"

namespace aos::sm::app {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

void AosCore::Init(const std::string& configFile)
{
    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize logger");

    LOG_INF() << "Init SM: version=" << AOS_CORE_CPP_VERSION;
    LOG_DBG() << "Aos core size: size=" << sizeof(AosCore);

    // Initialize Aos modules

    err = config::ParseConfig(configFile.empty() ? cDefaultConfigFile : configFile, mConfig);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse config");

    // Initialize crypto provider

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize crypto provider");

    // Initialize cert loader

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert loader");

    // Initialize IAM client

    err = mIAMClientPublic.Init(mConfig.mIAMClientConfig, mCertLoader, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize public IAM client");

    auto nodeInfo = std::make_shared<NodeInfo>();

    err = mIAMClientPublic.GetNodeInfo(*nodeInfo);
    AOS_ERROR_CHECK_AND_THROW(err, "can't get node info");

    err = mIAMClientPermissions.Init(mConfig.mIAMProtectedServerURL, mConfig.mCertStorage, mIAMClientPublic);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize permissions IAM client");

    // Initialize host device manager

    err = mHostDeviceManager.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize host device manager");

    // Initialize resource manager

    err = mResourceManager.Init(
        mJSONProvider, mHostDeviceManager, nodeInfo->mNodeType, mConfig.mNodeConfigFile.c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize resource manager");

    // Initialize database

    err = mDatabase.Init(mConfig.mWorkingDir, mConfig.mMigration);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize database");

    // Initialize traffic monitor

    err = mTrafficMonitor.Init(mDatabase, mIPTables);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize traffic monitor");

    // Initialize network manager

    err = mNetworkInterfaceManager.Init(mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize network interface manager");

    err = mNamespaceManager.Init(mNetworkInterfaceManager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize namespace manager");

    err = mCNI.Init(mExec);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize CNI");

    err = mNetworkManager.Init(mDatabase, mCNI, mTrafficMonitor, mNamespaceManager, mNetworkInterfaceManager,
        mCryptoProvider, mNetworkInterfaceManager, mConfig.mWorkingDir.c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize network manager");

    // Initialize resource usage provider

    err = mResourceUsageProvider.Init(mNetworkManager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize resource usage provider");

    // Initialize resource monitor

    err = mResourceMonitor.Init(mConfig.mMonitoring, mIAMClientPublic, mResourceManager, mResourceUsageProvider,
        mSMClient, mSMClient, mSMClient);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize resource monitor");

    // Initialize space allocators

    err = mDownloadServicesSpaceAllocator.Init(mConfig.mServiceManagerConfig.mDownloadDir, mPlatformFS);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize download services space allocator");

    err = mDownloadLayersSpaceAllocator.Init(mConfig.mLayerManagerConfig.mDownloadDir, mPlatformFS);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize download layers space allocator");

    err = mServicesSpaceAllocator.Init(mConfig.mServiceManagerConfig.mServicesDir, mPlatformFS,
        mConfig.mServiceManagerConfig.mPartLimit, &mServiceManager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize services space allocator");

    err = mLayersSpaceAllocator.Init(
        mConfig.mLayerManagerConfig.mLayersDir, mPlatformFS, mConfig.mLayerManagerConfig.mPartLimit, &mLayerManager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize layers space allocator");

    // Initialize image handler

    err = mImageHandler.Init(mCryptoProvider, mLayersSpaceAllocator, mServicesSpaceAllocator, mOCISpec);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize image handler");

    // Initialize service manager

    err = mServiceManager.Init(mConfig.mServiceManagerConfig, mOCISpec, mDownloader, mDatabase, mServicesSpaceAllocator,
        mDownloadServicesSpaceAllocator, mImageHandler);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize service manager");

    // Initialize layer manager

    err = mLayerManager.Init(mConfig.mLayerManagerConfig, mLayersSpaceAllocator, mDownloadLayersSpaceAllocator,
        mDatabase, mDownloader, mImageHandler);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize layer manager");

    // Initialize runner

    err = mRunner.Init(mLauncher);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize runner");

    // Initialize launcher

    err = mLauncher.Init(mConfig.mLauncherConfig, mIAMClientPublic, mServiceManager, mLayerManager, mResourceManager,
        mNetworkManager, mIAMClientPermissions, mRunner, mRuntime, mResourceMonitor, mOCISpec, mSMClient, mSMClient,
        mDatabase, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize launcher");

    // Initialize SM client

    err = mSMClient.Init(mConfig.mSMClientConfig, mIAMClientPublic, mIAMClientPublic, mResourceManager, mNetworkManager,
        mLogProvider, mResourceMonitor, mLauncher);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize SM client");

    // Initialize logprovider

    err = mLogProvider.Init(mConfig.mLogging, mDatabase);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize logprovider");

    // Initialize journalalerts

    err = mJournalAlerts.Init(mConfig.mJournalAlerts, mDatabase, mDatabase, mSMClient);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize journalalerts");
}

void AosCore::Start()
{
    auto err = mRunner.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start runner");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mRunner.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop runner: err=" << err;
        }
    });

    err = mLauncher.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start launcher");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mLauncher.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop launcher: err=" << err;
        }
    });

    err = mLayerManager.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start layer manager");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mLayerManager.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop layer manager: err=" << err;
        }
    });

    err = mNetworkManager.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start network manager");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mNetworkManager.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop network manager: err=" << err;
        }
    });

    err = mResourceMonitor.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start resource monitor");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mResourceMonitor.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop resource monitor: err=" << err;
        }
    });

    err = mServiceManager.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start service manager");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mServiceManager.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop service manager: err=" << err;
        }
    });

    err = mLogProvider.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start logprovider");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mLogProvider.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop logprovider: err=" << err;
        }
    });

    err = mJournalAlerts.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start journalalerts");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mJournalAlerts.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop journalalerts: err=" << err;
        }
    });

    err = mSMClient.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start SM client");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mSMClient.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop SM client: err=" << err;
        }
    });
}

void AosCore::Stop()
{
    mCleanupManager.ExecuteCleanups();
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
