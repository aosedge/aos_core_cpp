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

    LOG_INF() << "Init SM" << Log::Field("version", AOS_CORE_CPP_VERSION);
    LOG_DBG() << "Aos core size" << Log::Field("size", sizeof(AosCore));

    // Initialize Aos modules

    err = config::ParseConfig(configFile.empty() ? cDefaultConfigFile : configFile, mConfig);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse config");

    // Initialize crypto provider

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize crypto provider");

    // Initialize cert loader

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert loader");

    // Initialize TLS credentials

    err = mTLSCredentials.Init(mConfig.mIAMClientConfig.mCACert, mIAMClient, mCertLoader, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize TLS credentials");

    // Initialize IAM client

    err = mIAMClient.Init(mConfig.mIAMProtectedServerURL, mConfig.mIAMClientConfig.mIAMPublicServerURL,
        mConfig.mCertStorage, mTLSCredentials, "sm");
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM client");

    // NodeInfo nodeInfo;
    auto nodeInfo = std::make_unique<NodeInfo>();

    err = mIAMClient.GetCurrentNodeInfo(*nodeInfo);
    AOS_ERROR_CHECK_AND_THROW(err, "can't get node info");

    // Initialize resource manager

    err = mResourceManager.Init({mConfig.mNodeConfigFile.c_str()});
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

    // Initialize node monitoring provider

    err = mNodeMonitoringProvider.Init(mIAMClient, mTrafficMonitor);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize node monitoring provider");

    // Initialize runtimes

    err = mRuntimes.Init(mConfig.mLauncher, mIAMClient, mImageManager, mNetworkManager, mIAMClient, mResourceManager,
        mOCISpec, mLauncher, mSystemdConn);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize runtimes");

    auto runtimes = std::make_unique<StaticArray<launcher::RuntimeItf*, cMaxNumNodeRuntimes>>();

    err = mRuntimes.GetRuntimes(*runtimes);
    AOS_ERROR_CHECK_AND_THROW(err, "can't get runtimes");

    // Initialize images space allocator

    err = mImagesSpaceAllocator.Init(mConfig.mImageManager.mImagePath, mPlatformFS, 0, &mImageManager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize images space allocator");

    // Initialize downloader

    err = mDownloader.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize downloader");

    // Initialize file info provider

    err = mFileInfoProvider.Init(mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize file info provider");

    // Initialize image handler
    err = mImageHandler.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize image handler");

    // Initialize image manager

    err = mImageManager.Init(mConfig.mImageManager, mSMClient, mImagesSpaceAllocator, mDownloader, mFileInfoProvider,
        mOCISpec, mImageHandler);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize image manager");

    // Initialize launcher

    err = mLauncher.Init(*runtimes, mImageManager, mSMClient, mDatabase);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize launcher");

    // Initialize node config handler

    err = mNodeConfigHandler.Init({mConfig.mNodeConfigFile.c_str()}, mJSONProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize node config handler");

    // Initialize monitoring

    err = mMonitoring.Init(
        mConfig.mMonitoring, mNodeConfigHandler, mIAMClient, mSMClient, mSMClient, mNodeMonitoringProvider, &mLauncher);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize monitoring");

    auto containerRuntime = mRuntimes.GetContainerRuntime();
    if (!containerRuntime) {
        AOS_ERROR_THROW(ErrorEnum::eNotFound, "container runtime not available");
    }

    // Initialize logprovider

    err = mLogProvider.Init(mConfig.mLogging, *containerRuntime);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize logprovider");

    // Initialize SM client

    err = mSMClient.Init(mConfig.mSMClientConfig, nodeInfo->mNodeID.CStr(), mTLSCredentials, mIAMClient, mLauncher,
        mResourceManager, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring, mLauncher,
        mJSONProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize SM client");

    // // Initialize journalalerts

    err = mJournalAlerts.Init(mConfig.mJournalAlerts, *containerRuntime, mDatabase, mSMClient);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize journalalerts");
}

void AosCore::Start()
{
    auto err = mLauncher.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start launcher");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mLauncher.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop launcher: err=" << err;
        }
    });

    err = mNetworkManager.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start network manager");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mNetworkManager.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop network manager: err=" << err;
        }
    });

    err = mNodeMonitoringProvider.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start node monitoring provider");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mNodeMonitoringProvider.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop node monitoring provider: err=" << err;
        }
    });

    err = mMonitoring.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start monitoring");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mMonitoring.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop monitoring: err=" << err;
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
