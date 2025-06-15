/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_APP_APP_HPP_
#define AOS_MP_APP_APP_HPP_

#include <functional>
#include <optional>
#include <vector>

#include <Poco/Util/ServerApplication.h>

#include <aos/common/crypto/cryptoprovider.hpp>
#include <aos/iam/certmodules/pkcs11/pkcs11.hpp>

#include "common/downloader/downloader.hpp"
#include "common/iamclient/publicservicehandler.hpp"
#include "common/logger/logger.hpp"
#include "common/utils/cleanupmanager.hpp"
#include "mp/cmclient/cmclient.hpp"
#include "mp/communication/cmconnection.hpp"
#include "mp/communication/communicationmanager.hpp"
#include "mp/communication/iamconnection.hpp"
#include "mp/config/config.hpp"
#include "mp/iamclient/publicnodeclient.hpp"

#ifdef VCHAN
#include "mp/communication/vchan.hpp"
#else
#include "mp/communication/socket.hpp"
#endif

namespace aos::mp::app {

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

    common::logger::Logger mLogger;
    bool                   mStopProcessing = false;
    bool                   mProvisioning   = false;
    std::string            mConfigFile;

    crypto::DefaultCryptoProvider mCryptoProvider;
    crypto::CertLoader            mCertLoader;
    pkcs11::PKCS11Manager         mPKCS11Manager;

    config::Config mConfig = {};

    common::iamclient::PublicServiceHandler mPublicServiceHandler;
    cmclient::CMClient                      mCMClient;
    iamclient::PublicNodeClient             mPublicNodeClient;
    iamclient::PublicNodeClient             mProtectedNodeClient;

#ifdef VCHAN
    communication::VChan mTransport;
#else
    communication::Socket mTransport;
#endif
    communication::CommunicationManager mCommunicationManager;
    communication::IAMConnection        mIAMPublicConnection;
    communication::IAMConnection        mIAMProtectedConnection;
    communication::CMConnection         mCMConnection;
    common::downloader::Downloader      mDownloader;
    common::utils::CleanupManager       mCleanupManager;
};

} // namespace aos::mp::app

#endif
