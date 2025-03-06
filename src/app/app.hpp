/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_HPP_
#define APP_HPP_

#include <Poco/Util/ServerApplication.h>

#include <aos/common/crypto/mbedtls/cryptoprovider.hpp>
#include <aos/iam/certmodules/pkcs11/pkcs11.hpp>
#include <aos/iam/certprovider.hpp>
#include <aos/iam/nodemanager.hpp>
#include <aos/iam/permhandler.hpp>
#include <aos/iam/provisionmanager.hpp>
#include <logger/logger.hpp>

#include "database/database.hpp"
#include "iamclient/iamclient.hpp"
#include "iamserver/iamserver.hpp"
#include "nodeinfoprovider/nodeinfoprovider.hpp"
#include "visidentifier/visidentifier.hpp"

namespace aos::iam::app {

/**
 * Aos IAM application.
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
    static constexpr auto cDefaultConfigFile = "aos_iamanager.cfg";
    static constexpr auto cPKCS11CertModule  = "pkcs11module";

    void HandleHelp(const std::string& name, const std::string& value);
    void HandleVersion(const std::string& name, const std::string& value);
    void HandleProvisioning(const std::string& name, const std::string& value);
    void HandleJournal(const std::string& name, const std::string& value);
    void HandleLogLevel(const std::string& name, const std::string& value);
    void HandleConfigFile(const std::string& name, const std::string& value);

    Error InitCertModules(const config::Config& config);

    crypto::MbedTLSCryptoProvider mCryptoProvider;
    crypto::CertLoader            mCertLoader;
    certhandler::CertHandler      mCertHandler;
    pkcs11::PKCS11Manager         mPKCS11Manager;
    std::vector<std::pair<std::unique_ptr<certhandler::HSMItf>, std::unique_ptr<certhandler::CertModule>>> mCertModules;
    database::Database                                                                                     mDatabase;
    nodeinfoprovider::NodeInfoProvider             mNodeInfoProvider;
    nodemanager::NodeManager                       mNodeManager;
    certprovider::CertProvider                     mCertProvider;
    provisionmanager::ProvisionManager             mProvisionManager;
    iamserver::IAMServer                           mIAMServer;
    common::logger::Logger                         mLogger;
    std::unique_ptr<permhandler::PermHandler>      mPermHandler;
    std::unique_ptr<iamclient::IAMClient>          mIAMClient;
    std::unique_ptr<identhandler::IdentHandlerItf> mIdentifier;

    bool        mStopProcessing = false;
    bool        mProvisioning   = false;
    std::string mConfigFile;
};

} // namespace aos::iam::app

#endif
