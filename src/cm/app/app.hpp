/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_APP_APP_HPP_
#define AOS_CM_APP_APP_HPP_

#include <memory>
#include <vector>

#include <Poco/Util/ServerApplication.h>

#include <core/common/crypto/cryptoprovider.hpp>
#include <core/iam/certhandler/certmodules/pkcs11/pkcs11.hpp>
#include <core/iam/certhandler/certprovider.hpp>

#include <cm/config/config.hpp>
#include <common/iamclient/publicidentityhandler.hpp>
#include <common/iamclient/publicservicehandler.hpp>
#include <common/logger/logger.hpp>
#include <common/utils/cleanupmanager.hpp>

namespace aos::cm::app {

/**
 * Aos CM application.
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
    static constexpr auto cSDNotifyReady = "READY=1";

    void HandleHelp(const std::string& name, const std::string& value);
    void HandleConfigFile(const std::string& name, const std::string& value);
    void HandleLogLevel(const std::string& name, const std::string& value);
    void HandleReset(const std::string& name, const std::string& value);
    void HandleVersion(const std::string& name, const std::string& value);
    void HandleJournal(const std::string& name, const std::string& value);

    void Init();
    void Start();
    void Stop();

    common::logger::Logger mLogger;
    bool                   mStopProcessing = false;
    bool                   mProvisioning   = false;
    std::string            mConfigFile;

    common::utils::CleanupManager mCleanupManager;

    crypto::DefaultCryptoProvider mCryptoProvider;
    crypto::CertLoader            mCertLoader;
    iam::certhandler::CertHandler mCertHandler;
    pkcs11::PKCS11Manager         mPKCS11Manager;
    std::vector<std::pair<std::unique_ptr<iam::certhandler::HSMItf>, std::unique_ptr<iam::certhandler::CertModule>>>
        mCertModules;

    cm::config::Config                              mConfig;
    common::iamclient::PublicServiceHandler         mPublicServiceHandler;
    common::iamclient::PublicIdentityServiceHandler mPublicIdentityHandler;
};

} // namespace aos::cm::app

#endif
