/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <csignal>
#include <execinfo.h>
#include <iostream>

#include <Poco/Path.h>
#include <Poco/SignalHandler.h>
#include <Poco/Util/HelpFormatter.h>
#include <curl/curl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/trace.h>
#include <systemd/sd-daemon.h>

#include <core/common/version/version.hpp>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>
#include <common/version/version.hpp>

#include "app.hpp"

namespace aos::mp::app {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void SegmentationHandler(int sig)
{
    static constexpr auto cBacktraceSize = 32;

    void*  array[cBacktraceSize];
    size_t size;

    LOG_ERR() << "Segmentation fault";

    size = backtrace(array, cBacktraceSize);

    backtrace_symbols_fd(array, size, STDERR_FILENO);

    raise(sig);
}

void RegisterSegfaultSignal()
{
    struct sigaction act { };

    act.sa_handler = SegmentationHandler;
    act.sa_flags   = SA_RESETHAND;

    sigaction(SIGSEGV, &act, nullptr);
}

} // namespace

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

void App::initialize(Application& self)
{
    if (mStopProcessing) {
        return;
    }

    RegisterSegfaultSignal();

    Application::initialize(self);

    Init();

    Start();
}

void App::Init()
{
    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize logger");

    LOG_INF() << "Init MP" << Log::Field("version", AOS_CORE_CPP_VERSION);
    LOG_DBG() << "Aos core size" << Log::Field("size", sizeof(AosCore));

    CURLcode result = curl_global_init(CURL_GLOBAL_ALL);
    if (result != CURLE_OK) {
        AOS_ERROR_THROW(ErrorEnum::eFailed, "can't initialize curl");
    }

    mCleanupManager.AddCleanup([this]() { curl_global_cleanup(); });

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize crypto provider");

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert loader");

    auto retConfig = config::ParseConfig(mConfigFile);
    AOS_ERROR_CHECK_AND_THROW(retConfig.mError, "can't parse config");

    mConfig = retConfig.mValue;

    err = mPublicServiceHandler.Init(
        common::iamclient::Config {mConfig.mIAMConfig.mIAMPublicServerURL, mConfig.mCACert}, mCertLoader,
        mCryptoProvider, mProvisioning);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM client");

    err = mCMClient.Init(mConfig, mPublicServiceHandler, mCertLoader, mCryptoProvider, mProvisioning);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize CM client");

#ifdef VCHAN
    mTransport.Init(mConfig.mVChan);
#else
    mTransport.Init(30001);
#endif

    if (mProvisioning) {
        err = mCommunicationManager.Init(mConfig, mTransport);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize communication manager");

        err = mCMConnection.Init(mConfig, mCMClient, mCommunicationManager);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize CM connection");
    } else {
        err = mCommunicationManager.Init(mConfig, mTransport, &mCertLoader, &mCryptoProvider);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize communication manager");

        err = mCMConnection.Init(mConfig, mCMClient, mCommunicationManager, &mDownloader, &mPublicServiceHandler);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize CM connection");

        err = mProtectedNodeClient.Init(mConfig.mIAMConfig, mPublicServiceHandler, false);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize protected node client");

        err = mIAMProtectedConnection.Init(mConfig.mIAMConfig.mSecurePort, mProtectedNodeClient, mCommunicationManager,
            &mPublicServiceHandler, mConfig.mVChan.mIAMCertStorage);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM protected connection");
    }
    err = mPublicNodeClient.Init(mConfig.mIAMConfig, mPublicServiceHandler, true);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize public node client");

    err = mIAMPublicConnection.Init(mConfig.mIAMConfig.mOpenPort, mPublicNodeClient, mCommunicationManager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM public connection");

    // Subscribe to certificate changed

    if (!mProvisioning) {
        err = mPublicServiceHandler.SubscribeListener(mConfig.mCertStorage.c_str(), mCMClient);
        AOS_ERROR_CHECK_AND_THROW(err, "can't subscribe to certificate changed");

        err = mPublicServiceHandler.SubscribeListener(mConfig.mIAMConfig.mCertStorage.c_str(), mProtectedNodeClient);
        AOS_ERROR_CHECK_AND_THROW(err, "can't subscribe to certificate changed");

        err = mPublicServiceHandler.SubscribeListener(mConfig.mVChan.mIAMCertStorage.c_str(), mCommunicationManager);
        AOS_ERROR_CHECK_AND_THROW(err, "can't subscribe to certificate changed");

        err = mPublicServiceHandler.SubscribeListener(mConfig.mVChan.mSMCertStorage.c_str(), mCommunicationManager);
        AOS_ERROR_CHECK_AND_THROW(err, "can't subscribe to certificate changed");
    }

    // Notify systemd

    auto ret = sd_notify(0, cSDNotifyReady);
    if (ret < 0) {
        AOS_ERROR_CHECK_AND_THROW(ret, "can't notify systemd");
    }
}

void App::Start()
{
    auto err = mCommunicationManager.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start communication manager");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mCommunicationManager.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop communication manager: err=" << err;
        }
    });

    err = mCMConnection.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start CM connection");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mCMConnection.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop CM connection: err=" << err;
        }
    });

    if (!mProvisioning) {
        err = mIAMProtectedConnection.Start();
        AOS_ERROR_CHECK_AND_THROW(err, "can't start IAM protected connection");

        mCleanupManager.AddCleanup([this]() {
            if (auto err = mIAMProtectedConnection.Stop(); !err.IsNone()) {
                LOG_ERR() << "Can't stop IAM protected connection: err=" << err;
            }
        });
    }

    err = mIAMPublicConnection.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start IAM public connection");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mIAMPublicConnection.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop IAM public connection: err=" << err;
        }
    });
}

void App::uninitialize()
{
    LOG_INF() << "Uninitialize message-proxy";

    mCleanupManager.ExecuteCleanups();

    Application::uninitialize();
}

void App::reinitialize(Application& self)
{
    LOG_INF() << "Reinitialize message-proxy";

    Application::reinitialize(self);
}

int App::main(const ArgVec& args)
{
    (void)args;

    if (mStopProcessing) {
        return Application::EXIT_OK;
    }

    waitForTerminationRequest();

    return Application::EXIT_OK;
}

void App::defineOptions(Poco::Util::OptionSet& options)
{
    Application::defineOptions(options);

    options.addOption(Poco::Util::Option("help", "h", "displays help information")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleHelp)));
    options.addOption(Poco::Util::Option("version", "", "displays version information")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleVersion)));
    options.addOption(Poco::Util::Option("provisioning", "p", "enables provisioning mode")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleProvisioning)));
    options.addOption(Poco::Util::Option("journal", "j", "redirects logs to systemd journal")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleJournal)));
    options.addOption(Poco::Util::Option("verbose", "v", "sets current log level")
                          .argument("${level}")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleLogLevel)));
    options.addOption(Poco::Util::Option("config", "c", "path to config file")
                          .argument("${file}")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleConfigFile)));
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void App::HandleHelp(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mStopProcessing = true;

    Poco::Util::HelpFormatter helpFormatter(options());

    helpFormatter.setCommand(commandName());
    helpFormatter.setUsage("[OPTIONS]");
    helpFormatter.setHeader("Aos IAM manager service.");
    helpFormatter.format(std::cout);

    stopOptionsProcessing();
}

void App::HandleVersion(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mStopProcessing = true;

    std::cout << "Aos MP version:           " << AOS_CORE_CPP_VERSION << std::endl;
    std::cout << "Aos core library version: " << AOS_CORE_LIB_VERSION << std::endl;

    stopOptionsProcessing();
}

void App::HandleProvisioning(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mProvisioning = true;
}

void App::HandleJournal(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mLogger.SetBackend(common::logger::Logger::Backend::eJournald);
}

void App::HandleLogLevel(const std::string& name, const std::string& value)
{
    (void)name;

    LogLevel level;

    auto err = level.FromString(String(value.c_str()));
    if (!err.IsNone()) {
        throw Poco::Exception("unsupported log level", value);
    }

    mLogger.SetLogLevel(level);
}

void App::HandleConfigFile(const std::string& name, const std::string& value)
{
    (void)name;

    mConfigFile = value;
}

} // namespace aos::mp::app
