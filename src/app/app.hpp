/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_HPP_
#define APP_HPP_

#include <Poco/Util/ServerApplication.h>

#include <aos/common/crypto/mbedtls/cryptoprovider.hpp>
#include <aos/common/monitoring/resourcemonitor.hpp>
#include <aos/sm/launcher.hpp>
#include <aos/sm/layermanager.hpp>
#include <aos/sm/servicemanager.hpp>

#include <downloader/downloader.hpp>
#include <iamclient/permservicehandler.hpp>
#include <iamclient/publicservicehandler.hpp>
#include <jsonprovider/jsonprovider.hpp>

#include "database/database.hpp"
#include "downloader/downloader.hpp"
#include "image/imagehandler.hpp"
#include "logger/logger.hpp"
#include "logprovider/logprovider.hpp"
#include "monitoring/resourceusageprovider.hpp"
#include "networkmanager/cni.hpp"
#include "networkmanager/networkmanager.hpp"
#include "ocispec/ocispec.hpp"
#include "resourcemanager/resourcemanager.hpp"
#include "runner/runner.hpp"
#include "smclient/smclient.hpp"

namespace aos::sm::app {

/**
 * Aos SM application.
 */
class App : public Poco::Util::ServerApplication {
protected:
    void initialize(Application& self);
    void uninitialize();
    void reinitialize(Application& self);
    int  main(const ArgVec& args);
    void defineOptions(Poco::Util::OptionSet& options);

private:
    static constexpr auto cSDNotifyReady     = "READY=1";
    static constexpr auto cDefaultConfigFile = "aos_servicemanager.cfg";

    void HandleHelp(const std::string& name, const std::string& value);
    void HandleVersion(const std::string& name, const std::string& value);
    void HandleJournal(const std::string& name, const std::string& value);
    void HandleLogLevel(const std::string& name, const std::string& value);
    void HandleConfigFile(const std::string& name, const std::string& value);

    void InitAosCore();
    void StartAosCore();
    void StopAosCore();

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
    sm::database::Database                                               mDatabase;
    sm::image::ImageHandler                                              mImageHandler;
    sm::launcher::Launcher                                               mLauncher;
    sm::layermanager::LayerManager                                       mLayerManager;
    sm::logprovider::LogProvider                                         mLogProvider;
    sm::monitoring::ResourceUsageProvider                                mResourceUsageProvider;
    sm::networkmanager::NamespaceManager                                 mNamespaceManager;
    sm::networkmanager::NetworkInterfaceManager                          mNetworkInterfaceManager;
    sm::networkmanager::NetworkManager                                   mNetworkManager;
    sm::networkmanager::TrafficMonitor                                   mTrafficMonitor;
    sm::resourcemanager::HostDeviceManager                               mHostDeviceManager;
    sm::resourcemanager::ResourceManager                                 mResourceManager;
    sm::runner::Runner                                                   mRunner;
    sm::servicemanager::ServiceManager                                   mServiceManager;
    sm::smclient::SMClient                                               mSMClient;

    bool        mStopProcessing = false;
    std::string mConfigFile;
};

}; // namespace aos::sm::app

#endif
