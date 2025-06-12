/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_HPP_
#define APP_HPP_

#include <functional>
#include <optional>
#include <vector>

#include <Poco/Util/ServerApplication.h>

#include <logger/logger.hpp>

#include <aos/common/crypto/cryptoprovider.hpp>
#include <aos/iam/certmodules/pkcs11/pkcs11.hpp>
#include <downloader/downloader.hpp>
#include <iamclient/publicservicehandler.hpp>
#include <utils/cleanupmanager.hpp>

#include "cmclient/cmclient.hpp"
#include "communication/cmconnection.hpp"
#include "communication/communicationmanager.hpp"
#include "communication/iamconnection.hpp"
#ifdef VCHAN
#include "communication/vchan.hpp"
#else
#include "communication/socket.hpp"
#endif

#include "config/config.hpp"
#include "iamclient/publicnodeclient.hpp"

/**
 * Aos message-proxy application.
 */
class App : public Poco::Util::ServerApplication {
public:
    /**
     * Constructor.
     */
    App() = default;

protected:
    void initialize(Application& self) override;
    void uninitialize() override;
    void reinitialize(Application& self) override;
    int  main(const ArgVec& args) override;
    void defineOptions(Poco::Util::OptionSet& options) override;

private:
    static constexpr auto cSDNotifyReady     = "READY=1";
    static constexpr auto cDefaultConfigFile = "aos_message_proxy.cfg";

    void HandleHelp(const std::string& name, const std::string& value);
    void HandleVersion(const std::string& name, const std::string& value);
    void HandleProvisioning(const std::string& name, const std::string& value);
    void HandleJournal(const std::string& name, const std::string& value);
    void HandleLogLevel(const std::string& name, const std::string& value);
    void HandleConfigFile(const std::string& name, const std::string& value);

    void Init();
    void Start();

    aos::common::logger::Logger mLogger;
    bool                        mStopProcessing = false;
    bool                        mProvisioning   = false;
    std::string                 mConfigFile;

    aos::crypto::DefaultCryptoProvider mCryptoProvider;
    aos::crypto::CertLoader            mCertLoader;
    aos::pkcs11::PKCS11Manager         mPKCS11Manager;

    aos::mp::config::Config mConfig = {};

    aos::common::iamclient::PublicServiceHandler mPublicServiceHandler;
    aos::mp::cmclient::CMClient                  mCMClient;
    aos::mp::iamclient::PublicNodeClient         mPublicNodeClient;
    aos::mp::iamclient::PublicNodeClient         mProtectedNodeClient;

#ifdef VCHAN
    aos::mp::communication::VChan mTransport;
#else
    aos::mp::communication::Socket mTransport;
#endif
    aos::mp::communication::CommunicationManager mCommunicationManager;
    aos::mp::communication::IAMConnection        mIAMPublicConnection;
    aos::mp::communication::IAMConnection        mIAMProtectedConnection;
    aos::mp::communication::CMConnection         mCMConnection;
    aos::common::downloader::Downloader          mDownloader;
    aos::common::utils::CleanupManager           mCleanupManager;
};

#endif
