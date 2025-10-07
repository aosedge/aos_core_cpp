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
#include <core/common/monitoring/resourcemonitor.hpp>
#include <core/sm/launcher/launcher.hpp>
#include <core/sm/layermanager/layermanager.hpp>
#include <core/sm/servicemanager/servicemanager.hpp>

#include <common/downloader/downloader.hpp>
#include <common/iamclient/permservicehandler.hpp>
#include <common/iamclient/publicservicehandler.hpp>
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
#include <sm/image/imagehandler.hpp>
#include <sm/launcher/runtime.hpp>
#include <sm/logprovider/logprovider.hpp>
#include <sm/monitoring/resourceusageprovider.hpp>
#include <sm/networkmanager/cni.hpp>
#include <sm/networkmanager/exec.hpp>
#include <sm/networkmanager/trafficmonitor.hpp>
#include <sm/resourcemanager/resourcemanager.hpp>
#include <sm/runner/runner.hpp>
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
    config::Config                                       mConfig = {};
    aos::crypto::CertLoader                              mCertLoader;
    aos::crypto::DefaultCryptoProvider                   mCryptoProvider;
    aos::monitoring::ResourceMonitor                     mResourceMonitor;
    aos::pkcs11::PKCS11Manager                           mPKCS11Manager;
    aos::common::utils::FSPlatform                       mPlatformFS;
    aos::spaceallocator::SpaceAllocator<cMaxNumLayers>   mLayersSpaceAllocator;
    aos::spaceallocator::SpaceAllocator<cMaxNumServices> mDownloadServicesSpaceAllocator;
    aos::spaceallocator::SpaceAllocator<cMaxNumLayers>   mDownloadLayersSpaceAllocator;
    aos::spaceallocator::SpaceAllocator<cMaxNumServices> mServicesSpaceAllocator;
    common::downloader::Downloader                       mDownloader;
    common::iamclient::PermissionsServiceHandler         mIAMClientPermissions;
    common::iamclient::PublicServiceHandler              mIAMClientPublic;
    common::jsonprovider::JSONProvider                   mJSONProvider;
    common::logger::Logger                               mLogger;
    common::oci::OCISpec                                 mOCISpec;
    sm::cni::CNI                                         mCNI;
    sm::cni::Exec                                        mExec;
    sm::database::Database                               mDatabase;
    sm::image::ImageHandler                              mImageHandler;
    sm::launcher::Launcher                               mLauncher;
    sm::launcher::Runtime                                mRuntime;
    sm::layermanager::LayerManager                       mLayerManager;
    sm::logprovider::LogProvider                         mLogProvider;
    sm::monitoring::ResourceUsageProvider                mResourceUsageProvider;
    sm::networkmanager::NetworkManager                   mNetworkManager;
    sm::networkmanager::TrafficMonitor                   mTrafficMonitor;
    common::network::IPTables                            mIPTables;
    aos::common::network::NamespaceManager               mNamespaceManager;
    aos::common::network::InterfaceManager               mNetworkInterfaceManager;
    sm::resourcemanager::HostDeviceManager               mHostDeviceManager;
    sm::resourcemanager::ResourceManager                 mResourceManager;
    sm::runner::Runner                                   mRunner;
    sm::servicemanager::ServiceManager                   mServiceManager;
    sm::alerts::JournalAlerts                            mJournalAlerts;
    sm::smclient::SMClient                               mSMClient;
    aos::common::utils::CleanupManager                   mCleanupManager;

private:
    static constexpr auto cDefaultConfigFile = "aos_servicemanager.cfg";
};

} // namespace aos::sm::app

#endif
