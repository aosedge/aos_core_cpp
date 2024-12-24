/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_HPP_
#define APP_HPP_

#include <Poco/Util/ServerApplication.h>

#include <aos/common/crypto/mbedtls/cryptoprovider.hpp>

#include <iamclient/publicservicehandler.hpp>

#include "logger/logger.hpp"

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

    pkcs11::PKCS11Manager                   mPKCS11Manager;
    common::iamclient::PublicServiceHandler mIAMClientPublic;
    common::logger::Logger                  mLogger;
    crypto::CertLoader                      mCertLoader;
    crypto::MbedTLSCryptoProvider           mCryptoProvider;

    bool        mStopProcessing = false;
    std::string mConfigFile;
};

}; // namespace aos::sm::app

#endif
