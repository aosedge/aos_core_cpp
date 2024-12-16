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

    LOG_INF() << "Init SM: version = " << AOS_CORE_SM_VERSION;
    LOG_DBG() << "Aos core size: size=" << sizeof(AosCore);

    // Initialize Aos modules

    Tie(mConfig, err) = config::ParseConfig(configFile.empty() ? cDefaultConfigFile : configFile);
    AOS_ERROR_CHECK_AND_THROW("can't parse config", err);

    // Initialize crypto provider

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize crypto provider", err);

    // Initialize cert loader

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW("can't initialize cert loader", err);

    // Initialize IAM client

    err = mIAMClientPublic.Init(mConfig.mIAMClientConfig, mCertLoader, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW("can't initialize public IAM client", err);

    auto nodeInfo = std::make_shared<NodeInfo>();

    err = mIAMClientPublic.GetNodeInfo(*nodeInfo);
    AOS_ERROR_CHECK_AND_THROW("can't get node info", err);

    err = mIAMClientPermissions.Init(mConfig.mIAMProtectedServerURL, mConfig.mCertStorage, mIAMClientPublic);
    AOS_ERROR_CHECK_AND_THROW("can't initialize permissions IAM client", err);

    // Initialize host device manager

    err = mHostDeviceManager.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize host device manager", err);

    // Initialize resource manager

    err = mResourceManager.Init(
        mJSONProvider, mHostDeviceManager, nodeInfo->mNodeType, mConfig.mNodeConfigFile.c_str());
    AOS_ERROR_CHECK_AND_THROW("can't initialize resource manager", err);

    // Initialize database

    err = mDatabase.Init(mConfig.mWorkingDir, mConfig.mMigration);
    AOS_ERROR_CHECK_AND_THROW("can't initialize database", err);

    // Initialize traffic monitor

    err = mTrafficMonitor.Init(mDatabase, mIPTables);
    AOS_ERROR_CHECK_AND_THROW("can't initialize traffic monitor", err);

    // Initialize network manager

    err = mCNI.Init(mExec);
    AOS_ERROR_CHECK_AND_THROW("can't initialize CNI", err);

    err = mNetworkManager.Init(
        mDatabase, mCNI, mTrafficMonitor, mNamespaceManager, mNetworkInterfaceManager, mConfig.mWorkingDir.c_str());
    AOS_ERROR_CHECK_AND_THROW("can't initialize network manager", err);

    // Initialize resource usage provider

    err = mResourceUsageProvider.Init(mNetworkManager);
    AOS_ERROR_CHECK_AND_THROW("can't initialize resource usage provider", err);

    // Initialize resource monitor

    err = mResourceMonitor.Init(mConfig.mMonitoring, mIAMClientPublic, mResourceManager, mResourceUsageProvider,
        mSMClient, mSMClient, mSMClient);
    AOS_ERROR_CHECK_AND_THROW("can't initialize resource monitor", err);

    // Initialize image handler

    err = mImageHandler.Init(mCryptoProvider, mLayersSpaceAllocator, mDownloadSpaceAllocator, mOCISpec);
    AOS_ERROR_CHECK_AND_THROW("can't initialize image handler", err);

    // Initialize service manager

    err = mServiceManager.Init(mConfig.mServiceManagerConfig, mOCISpec, mDownloader, mDatabase, mServicesSpaceAllocator,
        mDownloadSpaceAllocator, mImageHandler);
    AOS_ERROR_CHECK_AND_THROW("can't initialize service manager", err);

    // Initialize layer manager

    err = mLayerManager.Init(mConfig.mLayerManagerConfig, mLayersSpaceAllocator, mDownloadSpaceAllocator, mDatabase,
        mDownloader, mImageHandler);
    AOS_ERROR_CHECK_AND_THROW("can't initialize layer manager", err);

    // Initialize runner

    err = mRunner.Init(mLauncher);
    AOS_ERROR_CHECK_AND_THROW("can't initialize runner", err);

    // Initialize launcher

    err = mLauncher.Init(mConfig.mLauncherConfig, mIAMClientPublic, mServiceManager, mLayerManager, mResourceManager,
        mNetworkManager, mIAMClientPermissions, mRunner, mRuntime, mResourceMonitor, mOCISpec, mSMClient, mSMClient,
        mDatabase);
    AOS_ERROR_CHECK_AND_THROW("can't initialize launcher", err);

    // Initialize SM client

    err = mSMClient.Init(mConfig.mSMClientConfig, mIAMClientPublic, mIAMClientPublic, mResourceManager, mNetworkManager,
        mLogProvider, mResourceMonitor, mLauncher);
    AOS_ERROR_CHECK_AND_THROW("can't initialize SM client", err);

    // Initialize logprovider

    err = mLogProvider.Init(mConfig.mLogging, mDatabase);
    AOS_ERROR_CHECK_AND_THROW("can't initialize logprovider", err);
}

void AosCore::Start()
{
    auto err = mSMClient.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start SM client", err);

    err = mRunner.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start runner", err);

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

    err = mLogProvider.Start();
    AOS_ERROR_CHECK_AND_THROW("can't start logprovider", err);
}

void AosCore::Stop()
{
    Error stopError;

    if (auto err = mSMClient.Stop(); !err.IsNone() && stopError.IsNone()) {
        stopError = err;
    }

    if (auto err = mLauncher.Stop(); !err.IsNone() && stopError.IsNone()) {
        stopError = err;
    }

    if (auto err = mRunner.Stop(); !err.IsNone() && stopError.IsNone()) {
        stopError = err;
    }

    if (auto err = mLayerManager.Stop(); !err.IsNone() && stopError.IsNone()) {
        stopError = err;
    }

    if (auto err = mNetworkManager.Stop(); !err.IsNone() && stopError.IsNone()) {
        stopError = err;
    }

    if (auto err = mResourceMonitor.Stop(); !err.IsNone() && stopError.IsNone()) {
        stopError = err;
    }

    if (auto err = mServiceManager.Stop(); !err.IsNone() && stopError.IsNone()) {
        stopError = err;
    }

    if (auto err = mLogProvider.Stop(); !err.IsNone() && stopError.IsNone()) {
        stopError = err;
    }

    if (!stopError.IsNone()) {
        AOS_ERROR_THROW("can't stop Aos core", stopError);
    }
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
