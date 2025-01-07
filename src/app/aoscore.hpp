/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOSCORE_HPP_
#define AOSCORE_HPP_

#include <optional>

#include <aos/common/crypto/mbedtls/cryptoprovider.hpp>
#include <aos/common/monitoring/resourcemonitor.hpp>
#include <aos/sm/launcher.hpp>
#include <aos/sm/layermanager.hpp>
#include <aos/sm/servicemanager.hpp>

#include <downloader/downloader.hpp>
#include <iamclient/permservicehandler.hpp>
#include <iamclient/publicservicehandler.hpp>
#include <jsonprovider/jsonprovider.hpp>
#include <network/iptables.hpp>

#include "database/database.hpp"
#include "downloader/downloader.hpp"
#include "image/imagehandler.hpp"
#include "launcher/runtime.hpp"
#include "logger/logger.hpp"
#include "logprovider/logprovider.hpp"
#include "monitoring/resourceusageprovider.hpp"
#include "networkmanager/cni.hpp"
#include "networkmanager/exec.hpp"
#include "networkmanager/networkmanager.hpp"
#include "networkmanager/trafficmonitor.hpp"
#include "ocispec/ocispec.hpp"
#include "resourcemanager/resourcemanager.hpp"
#include "runner/runner.hpp"
#include "smclient/smclient.hpp"

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
    config::Config                                                       mConfig = {};
    aos::crypto::CertLoader                                              mCertLoader;
    aos::crypto::MbedTLSCryptoProvider                                   mCryptoProvider;
    aos::monitoring::ResourceMonitor                                     mResourceMonitor;
    aos::pkcs11::PKCS11Manager                                           mPKCS11Manager;
    aos::spaceallocator::SpaceAllocator<cMaxNumLayers>                   mLayersSpaceAllocator;
    aos::spaceallocator::SpaceAllocator<cMaxNumServices + cMaxNumLayers> mDownloadSpaceAllocator;
    aos::spaceallocator::SpaceAllocator<cMaxNumServices>                 mServicesSpaceAllocator;
    common::downloader::Downloader                                       mDownloader;
    common::iamclient::PermissionsServiceHandler                         mIAMClientPermissions;
    common::iamclient::PublicServiceHandler                              mIAMClientPublic;
    common::jsonprovider::JSONProvider                                   mJSONProvider;
    common::logger::Logger                                               mLogger;
    common::oci::OCISpec                                                 mOCISpec;
    sm::cni::CNI                                                         mCNI;
    sm::cni::Exec                                                        mExec;
    sm::database::Database                                               mDatabase;
    sm::image::ImageHandler                                              mImageHandler;
    sm::launcher::Launcher                                               mLauncher;
    sm::launcher::Runtime                                                mRuntime;
    sm::layermanager::LayerManager                                       mLayerManager;
    sm::logprovider::LogProvider                                         mLogProvider;
    sm::monitoring::ResourceUsageProvider                                mResourceUsageProvider;
    sm::networkmanager::NamespaceManager                                 mNamespaceManager;
    sm::networkmanager::NetworkInterfaceManager                          mNetworkInterfaceManager;
    sm::networkmanager::NetworkManager                                   mNetworkManager;
    sm::networkmanager::TrafficMonitor                                   mTrafficMonitor;
    common::network::IPTables                                            mIPTables;
    sm::resourcemanager::HostDeviceManager                               mHostDeviceManager;
    sm::resourcemanager::ResourceManager                                 mResourceManager;
    sm::runner::Runner                                                   mRunner;
    sm::servicemanager::ServiceManager                                   mServiceManager;
    sm::smclient::SMClient                                               mSMClient;

private:
    static constexpr auto cDefaultConfigFile = "aos_servicemanager.cfg";
};

} // namespace aos::sm::app

#endif
