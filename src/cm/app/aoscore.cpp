/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/version/version.hpp>

#include <cm/utils/uidgidvalidator.hpp>

#include "aoscore.hpp"
#include "envvarhandler.hpp"

namespace aos::cm::app {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

void AosCore::Init(const std::string& configFile)
{
    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize logger");

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

    // Initialize crypto helper

    err = mCryptoHelper.Init(
        mIAMClient, mCryptoProvider, mCertLoader, mConfig.mServiceDiscoveryURL.c_str(), mConfig.mCACert.c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize crypto helper");

    // Initialize file info provider

    err = mFileInfoProvider.Init(mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize file info provider");

    // Initialize TLS credentials

    err = mTLSCredentials.Init(mConfig.mCACert, mIAMClient, mCertLoader, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize TLS credentials");

    // Initialize IAM client

    err = mIAMClient.Init(mConfig.mIAMProtectedServerURL, mConfig.mIAMPublicServerURL, mConfig.mCertStorage,
        mTLSCredentials, mConfig.mCertStorage.c_str(), false);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM client");

    // Initialize communication

    err = mCommunication.Init(mConfig, mIAMClient, mIAMClient, mIAMClient, mCertLoader, mCryptoProvider, mCryptoHelper,
        mCryptoProvider, mUpdateManager, mStorageState, mSMController, mLauncher, mIAMClient, mIAMClient);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize communication");

    InitDatabase();
    InitStorageState();
    InitSMController();

    err = mAlerts.Init(mConfig.mAlerts, mCommunication, mCommunication);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize alerts");

    err = mDownloadSpaceAllocator.Init(mConfig.mImageManager.mInstallPath, mPlatformFS, 0, &mImageManager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize download space allocator");

    err = mInstallSpaceAllocator.Init(mConfig.mImageManager.mInstallPath, mPlatformFS, 0, &mImageManager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize install space allocator");

    err = mDownloader.Init(&mAlerts);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize downloader");

    err = mFileServer.Init(mConfig.mFileServerURL, mConfig.mImageManager.mInstallPath.CStr());
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize file server");

    err = mImageManager.Init(mConfig.mImageManager, mDatabase, mCommunication, mDownloadSpaceAllocator,
        mInstallSpaceAllocator, mDownloader, mFileServer, mCryptoHelper, mFileInfoProvider, mOCISpec);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize image manager");

    err = mNodeInfoProvider.Init(mConfig.mNodeInfoProvider, mIAMClient);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize node info provider");

    err = mMonitoring.Init(mConfig.mMonitoring, mCommunication, mCommunication, mLauncher, mNodeInfoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize monitoring");

    err = mUnitConfig.Init({mConfig.mUnitConfigFile.c_str()}, mNodeInfoProvider, mSMController, mJSONProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize unit config");

    err = mLauncher.Init(mConfig.mLauncher, mNodeInfoProvider, mSMController, mImageManager, mOCISpec, mUnitConfig,
        mStorageState, mNetworkManager, mSMController, mAlerts, mIAMClient, utils::IsUIDValid, utils::IsGIDValid,
        mDatabase);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize launcher");

    err = mUpdateManager.Init({mConfig.mUnitStatusSendTimeout}, mIAMClient, mIAMClient, mUnitConfig, mNodeInfoProvider,
        mImageManager, mLauncher, mCommunication, mCommunication);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize update manager");

    mDNSServer.Init(mConfig.mDNSStoragePath, mConfig.mDNSIP);

    err = mNetworkManager.Init(mDatabase, mCryptoProvider, mSMController, mDNSServer);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize network manager");
}

void AosCore::Start()
{
    auto err = mFSWatcher.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start FS watcher");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mFSWatcher.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop FS watcher" << Log::Field(err);
        }
    });

    err = mFileServer.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start file server");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mFileServer.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop file server" << Log::Field(err);
        }
    });

    err = mStorageState.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start storage state");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mStorageState.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop storage state" << Log::Field(err);
        }
    });

    err = mAlerts.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start alerts");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mAlerts.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop alerts" << Log::Field(err);
        }
    });

    err = mNodeInfoProvider.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start node info provider");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mNodeInfoProvider.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop node info provider" << Log::Field(err);
        }
    });

    err = mMonitoring.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start monitoring");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mMonitoring.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop monitoring" << Log::Field(err);
        }
    });

    err = mImageManager.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start image manager");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mImageManager.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop image manager" << Log::Field(err);
        }
    });

    err = mLauncher.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start launcher");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mLauncher.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop launcher" << Log::Field(err);
        }
    });

    err = mSMController.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start SM controller");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mSMController.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop SM controller" << Log::Field(err);
        }
    });

    err = mUpdateManager.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start update manager");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mUpdateManager.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop update manager" << Log::Field(err);
        }
    });

    err = mCommunication.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start communication");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mCommunication.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop communication" << Log::Field(err);
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

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void AosCore::InitDatabase()
{
    database::Config config;

    config.mWorkingDir          = mConfig.mWorkingDir;
    config.mMigrationPath       = mConfig.mMigration.mMigrationPath;
    config.mMergedMigrationPath = mConfig.mMigration.mMergedMigrationPath;

    auto err = mDatabase.Init(config);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize database");
}

void AosCore::InitStorageState()
{
    mFSWatcher.Init(Time::cMinutes, 10 * Time::cSeconds, {fs::FSEventEnum::eModify});

    storagestate::Config config;

    auto err = config.mStateDir.Assign(mConfig.mStateDir.c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't assign state dir to storage state config");

    err = config.mStorageDir.Assign(mConfig.mStorageDir.c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't assign storage dir to storage state config");

    err = mStorageState.Init(config, mDatabase, mCommunication, mPlatformFS, mFSWatcher, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize storage state");
}

void AosCore::InitSMController()
{
    smcontroller::Config config;

    config.mCACert      = mConfig.mCACert;
    config.mCertStorage = mConfig.mCertStorage;
    config.mCMServerURL = mConfig.mCMServerURL;

    auto err = mSMController.Init(config, mCommunication, mIAMClient, mCertLoader, mCryptoProvider, mImageManager,
        mAlerts, mCommunication, mCommunication, mMonitoring, mLauncher, mNodeInfoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize SM controller");
}

} // namespace aos::cm::app
