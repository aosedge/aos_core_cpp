/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_APP_AOSCORE_HPP_
#define AOS_SM_APP_AOSCORE_HPP_

#include <optional>

#include <core/common/crypto/certloader.hpp>
#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/monitoring/monitoring.hpp>
#include <core/common/spaceallocator/spaceallocator.hpp>
#include <core/sm/imagemanager/imagemanager.hpp>
#include <core/sm/launcher/launcher.hpp>
#include <core/sm/nodeconfig/nodeconfig.hpp>

#include <common/downloader/downloader.hpp>
#include <common/iamclient/tlscredentials.hpp>
#include <common/jsonprovider/jsonprovider.hpp>
#include <common/logger/logger.hpp>
#include <common/network/interfacemanager.hpp>
#include <common/network/iptables.hpp>
#include <common/network/namespacemanager.hpp>
#include <common/ocispec/ocispec.hpp>
#include <common/utils/cleanupmanager.hpp>
#include <common/utils/fsplatform.hpp>
#include <sm/alerts/journalalerts.hpp>
#include <sm/database/database.hpp>
#include <sm/iamclient/iamclient.hpp>
#include <sm/imagemanager/imagehandler.hpp>
#include <sm/launcher/runtimes.hpp>
#include <sm/logprovider/logprovider.hpp>
#include <sm/monitoring/nodemonitoringprovider.hpp>
#include <sm/networkmanager/cni.hpp>
#include <sm/networkmanager/exec.hpp>
#include <sm/networkmanager/trafficmonitor.hpp>
#include <sm/resourcemanager/resourcemanager.hpp>
#include <sm/smclient/smclient.hpp>

namespace aos::sm::app {

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
    config::Config mConfig = {};

    aos::crypto::CertLoader                                     mCertLoader;
    aos::crypto::DefaultCryptoProvider                          mCryptoProvider;
    aos::fs::FileInfoProvider                                   mFileInfoProvider;
    aos::monitoring::Monitoring                                 mMonitoring;
    aos::pkcs11::PKCS11Manager                                  mPKCS11Manager;
    aos::spaceallocator::SpaceAllocator<cMaxNumConcurrentItems> mImagesSpaceAllocator;

    common::downloader::Downloader     mDownloader;
    common::iamclient::TLSCredentials  mTLSCredentials;
    common::jsonprovider::JSONProvider mJSONProvider;
    common::logger::Logger             mLogger;
    common::network::InterfaceManager  mNetworkInterfaceManager;
    common::network::IPTables          mIPTables;
    common::network::NamespaceManager  mNamespaceManager;
    common::oci::OCISpec               mOCISpec;
    common::utils::CleanupManager      mCleanupManager;
    common::utils::FSPlatform          mPlatformFS;

    sm::alerts::JournalAlerts              mJournalAlerts;
    sm::cni::CNI                           mCNI;
    sm::cni::Exec                          mExec;
    sm::database::Database                 mDatabase;
    sm::iamclient::IAMClient               mIAMClient;
    sm::imagemanager::ImageHandler         mImageHandler;
    sm::imagemanager::ImageManager         mImageManager;
    sm::launcher::Launcher                 mLauncher;
    sm::launcher::Runtimes                 mRuntimes;
    sm::logprovider::LogProvider           mLogProvider;
    sm::monitoring::NodeMonitoringProvider mNodeMonitoringProvider;
    sm::networkmanager::NetworkManager     mNetworkManager;
    sm::networkmanager::TrafficMonitor     mTrafficMonitor;
    sm::nodeconfig::NodeConfig             mNodeConfigHandler;
    sm::resourcemanager::ResourceManager   mResourceManager;
    sm::smclient::SMClient                 mSMClient;

private:
    static constexpr auto cDefaultConfigFile = "aos_servicemanager.cfg";
};

} // namespace aos::sm::app

#endif
