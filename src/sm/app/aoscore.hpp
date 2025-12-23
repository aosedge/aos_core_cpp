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
#include <core/sm/launcher/launcher.hpp>
#include <core/sm/nodeconfig/nodeconfig.hpp>

#include <common/iamclient/tlscredentials.hpp>
#include <common/jsonprovider/jsonprovider.hpp>
#include <common/logger/logger.hpp>
#include <common/network/interfacemanager.hpp>
#include <common/network/iptables.hpp>
#include <common/network/namespacemanager.hpp>
#include <common/utils/cleanupmanager.hpp>
#include <sm/alerts/journalalerts.hpp>
#include <sm/database/database.hpp>
#include <sm/iamclient/iamclient.hpp>
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
    config::Config                         mConfig = {};
    aos::crypto::CertLoader                mCertLoader;
    aos::crypto::DefaultCryptoProvider     mCryptoProvider;
    aos::monitoring::Monitoring            mMonitoring;
    aos::pkcs11::PKCS11Manager             mPKCS11Manager;
    common::iamclient::TLSCredentials      mTLSCredentials;
    sm::iamclient::IAMClient               mIAMClient;
    common::jsonprovider::JSONProvider     mJSONProvider;
    common::logger::Logger                 mLogger;
    sm::cni::CNI                           mCNI;
    sm::cni::Exec                          mExec;
    sm::database::Database                 mDatabase;
    sm::launcher::Runtimes                 mRuntimes;
    sm::launcher::Launcher                 mLauncher;
    sm::logprovider::LogProvider           mLogProvider;
    sm::monitoring::NodeMonitoringProvider mNodeMonitoringProvider;
    sm::networkmanager::NetworkManager     mNetworkManager;
    sm::networkmanager::TrafficMonitor     mTrafficMonitor;
    common::network::IPTables              mIPTables;
    aos::common::network::NamespaceManager mNamespaceManager;
    aos::common::network::InterfaceManager mNetworkInterfaceManager;
    sm::resourcemanager::ResourceManager   mResourceManager;
    sm::nodeconfig::NodeConfig             mNodeConfigHandler;
    sm::alerts::JournalAlerts              mJournalAlerts;
    sm::smclient::SMClient                 mSMClient;
    aos::common::utils::CleanupManager     mCleanupManager;

private:
    static constexpr auto cDefaultConfigFile = "aos_servicemanager.cfg";
};

} // namespace aos::sm::app

#endif
