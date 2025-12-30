/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_APP_AOSCORE_HPP_
#define AOS_CM_APP_AOSCORE_HPP_

#include <memory>
#include <optional>

#include <core/cm/alerts/alerts.hpp>
#include <core/cm/imagemanager/imagemanager.hpp>
#include <core/cm/launcher/launcher.hpp>
#include <core/cm/monitoring/monitoring.hpp>
#include <core/cm/nodeinfoprovider/nodeinfoprovider.hpp>
#include <core/cm/smcontroller/itf/smcontroller.hpp>
#include <core/cm/storagestate/storagestate.hpp>
#include <core/cm/unitconfig/unitconfig.hpp>
#include <core/cm/updatemanager/updatemanager.hpp>
#include <core/common/crypto/certloader.hpp>
#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/spaceallocator/spaceallocator.hpp>

#include <common/downloader/downloader.hpp>
#include <common/fileserver/fileserver.hpp>
#include <common/iamclient/tlscredentials.hpp>
#include <common/jsonprovider/jsonprovider.hpp>
#include <common/logger/logger.hpp>
#include <common/network/interfacemanager.hpp>
#include <common/network/iptables.hpp>
#include <common/network/namespacemanager.hpp>
#include <common/ocispec/ocispec.hpp>
#include <common/utils/cleanupmanager.hpp>
#include <common/utils/fsplatform.hpp>
#include <common/utils/fswatcher.hpp>

#include <cm/communication/communication.hpp>
#include <cm/config/config.hpp>
#include <cm/database/database.hpp>
#include <cm/iamclient/iamclient.hpp>
#include <cm/networkmanager/dnsserver.hpp>
#include <cm/networkmanager/networkmanager.hpp>
#include <cm/smcontroller/smcontroller.hpp>
#include <cm/unitconfig/jsonprovider.hpp>

namespace aos::cm::app {

/**
 * Aos core instance.
 */
class AosCore {
public:
    /**
     * Initializes Aos core.
     */
    void Init(const std::string& configFile);

    /**
     * Starts Aos core.
     */
    void Start();

    /**
     * Stops Aos core.
     */
    void Stop();

    /**
     * Sets log backend.
     *
     * @param backend log backend.
     */
    void SetLogBackend(aos::common::logger::Logger::Backend backend);

    /**
     * Sets log level.
     *
     * @param level log level.
     */
    void SetLogLevel(aos::LogLevel level);

private:
    void InitDatabase();
    void InitStorageState();
    void InitSMController();

    config::Config                                              mConfig = {};
    aos::crypto::CertLoader                                     mCertLoader;
    aos::crypto::DefaultCryptoProvider                          mCryptoProvider;
    aos::crypto::CryptoHelper                                   mCryptoHelper;
    aos::pkcs11::PKCS11Manager                                  mPKCS11Manager;
    aos::spaceallocator::SpaceAllocator<cMaxNumConcurrentItems> mDownloadSpaceAllocator;
    aos::spaceallocator::SpaceAllocator<cMaxNumConcurrentItems> mInstallSpaceAllocator;
    aos::common::downloader::Downloader                         mDownloader;
    aos::common::utils::FSPlatform                              mPlatformFS;
    aos::common::utils::FSBufferedWatcher                       mFSWatcher;
    aos::fs::FileInfoProvider                                   mFileInfoProvider;
    aos::common::oci::OCISpec                                   mOCISpec;
    common::fileserver::Fileserver                              mFileServer;
    common::iamclient::TLSCredentials                           mTLSCredentials;
    cm::alerts::Alerts                                          mAlerts;
    cm::imagemanager::ImageManager                              mImageManager;
    cm::launcher::Launcher                                      mLauncher;
    cm::monitoring::Monitoring                                  mMonitoring;
    cm::networkmanager::NetworkManager                          mNetworkManager;
    cm::networkmanager::DNSServer                               mDNSServer;
    cm::nodeinfoprovider::NodeInfoProvider                      mNodeInfoProvider;
    cm::smcontroller::SMController                              mSMController;
    cm::storagestate::StorageState                              mStorageState;
    cm::unitconfig::JSONProvider                                mJSONProvider;
    cm::unitconfig::UnitConfig                                  mUnitConfig;
    cm::updatemanager::UpdateManager                            mUpdateManager;
    communication::Communication                                mCommunication;
    database::Database                                          mDatabase;
    iamclient::IAMClient                                        mIAMClient;
    common::logger::Logger                                      mLogger;
    aos::common::utils::CleanupManager                          mCleanupManager;

private:
    static constexpr auto cDefaultConfigFile = "aos_cm.cfg";
};

} // namespace aos::cm::app

#endif
