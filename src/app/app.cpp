/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <csignal>
#include <execinfo.h>
#include <iostream>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/trace.h>

#include <curl/curl.h>

#include <Poco/Path.h>
#include <Poco/SignalHandler.h>
#include <Poco/Util/HelpFormatter.h>
#include <systemd/sd-daemon.h>

#include <aos/common/version.hpp>
#include <logger/logmodule.hpp>
#include <utils/exception.hpp>

#include "app.hpp"
// cppcheck-suppress missingInclude
#include "version.hpp"

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static void SegmentationHandler(int sig)
{
    static constexpr auto cBacktraceSize = 32;

    void*  array[cBacktraceSize];
    size_t size;

    LOG_ERR() << "Segmentation fault";

    size = backtrace(array, cBacktraceSize);

    backtrace_symbols_fd(array, size, STDERR_FILENO);

    raise(sig);
}

static void RegisterSegfaultSignal()
{
    struct sigaction act { };

    act.sa_handler = SegmentationHandler;
    act.sa_flags   = SA_RESETHAND;

    sigaction(SIGSEGV, &act, nullptr);
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

void App::initialize(Application& self)
{
    if (mStopProcessing) {
        return;
    }

    RegisterSegfaultSignal();

    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize logger", err);

    Application::initialize(self);

    LOG_INF() << "Initialize message-proxy: version = " << AOS_MESSAGE_PROXY_VERSION;

    CURLcode result = curl_global_init(CURL_GLOBAL_ALL);
    if (result != CURLE_OK) {
        AOS_ERROR_THROW("can't initialize curl", aos::ErrorEnum::eFailed);
    }

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize crypto provider", err);

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW("can't initialize cert loader", err);

    auto retConfig = aos::mp::config::ParseConfig(mConfigFile);
    AOS_ERROR_CHECK_AND_THROW("can't parse config", retConfig.mError);

    mConfig = retConfig.mValue;

    err = mPublicServiceHandler.Init(
        aos::common::iamclient::Config {mConfig.mIAMConfig.mIAMPublicServerURL, mConfig.mCACert}, mCertLoader,
        mCryptoProvider, mProvisioning);
    AOS_ERROR_CHECK_AND_THROW("can't initialize IAM client", err);

    err = mCMClient.Init(mConfig, mPublicServiceHandler, mCertLoader, mCryptoProvider, mProvisioning);
    AOS_ERROR_CHECK_AND_THROW("can't initialize CM client", err);

#ifdef VCHAN
    mTransport.Init(mConfig.mVChan);
#else
    mTransport.Init(30001);
#endif

    if (mProvisioning) {
        err = mCommunicationManager.Init(mConfig, mTransport);
        AOS_ERROR_CHECK_AND_THROW("can't initialize communication manager", err);

        err = mCMConnection.Init(mConfig, mCMClient, mCommunicationManager);
        AOS_ERROR_CHECK_AND_THROW("can't initialize CM connection", err);
    } else {
        err = mCommunicationManager.Init(mConfig, mTransport, &mCertLoader, &mCryptoProvider);
        AOS_ERROR_CHECK_AND_THROW("can't initialize communication manager", err);

        err = mCMConnection.Init(mConfig, mCMClient, mCommunicationManager, &mPublicServiceHandler);
        AOS_ERROR_CHECK_AND_THROW("can't initialize CM connection", err);

        err = mProtectedNodeClient.Init(mConfig.mIAMConfig, mPublicServiceHandler, false);
        AOS_ERROR_CHECK_AND_THROW("can't initialize protected node client", err);

        err = mIAMProtectedConnection.Init(mConfig.mIAMConfig.mSecurePort, mProtectedNodeClient, mCommunicationManager,
            &mPublicServiceHandler, mConfig.mVChan.mIAMCertStorage);
        AOS_ERROR_CHECK_AND_THROW("can't initialize IAM protected connection", err);
    }
    err = mPublicNodeClient.Init(mConfig.mIAMConfig, mPublicServiceHandler, true);
    AOS_ERROR_CHECK_AND_THROW("can't initialize public node client", err);

    err = mIAMPublicConnection.Init(mConfig.mIAMConfig.mOpenPort, mPublicNodeClient, mCommunicationManager);
    AOS_ERROR_CHECK_AND_THROW("can't initialize IAM public connection", err);

    // Subscribe to certificate changed

    if (!mProvisioning) {
        err = mPublicServiceHandler.SubscribeCertChanged(mConfig.mCertStorage.c_str(), mCMClient);
        AOS_ERROR_CHECK_AND_THROW("can't subscribe to certificate changed", err);

        err = mPublicServiceHandler.SubscribeCertChanged(mConfig.mIAMConfig.mCertStorage.c_str(), mProtectedNodeClient);
        AOS_ERROR_CHECK_AND_THROW("can't subscribe to certificate changed", err);

        err = mPublicServiceHandler.SubscribeCertChanged(mConfig.mVChan.mIAMCertStorage.c_str(), mCommunicationManager);
        AOS_ERROR_CHECK_AND_THROW("can't subscribe to certificate changed", err);

        err = mPublicServiceHandler.SubscribeCertChanged(mConfig.mVChan.mSMCertStorage.c_str(), mCommunicationManager);
        AOS_ERROR_CHECK_AND_THROW("can't subscribe to certificate changed", err);
    }

    // Notify systemd

    auto ret = sd_notify(0, cSDNotifyReady);
    if (ret < 0) {
        AOS_ERROR_CHECK_AND_THROW("can't notify systemd", ret);
    }
}

void App::uninitialize()
{
    LOG_INF() << "Uninitialize message-proxy";

    mTransport.Shutdown();
    mCommunicationManager.Close();

    mCMConnection.Close();
    if (!mProvisioning) {
        mIAMProtectedConnection.Close();
    }

    mIAMPublicConnection.Close();

    curl_global_cleanup();

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

    std::cout << "Aos IA manager version:   " << AOS_MESSAGE_PROXY_VERSION << std::endl;
    std::cout << "Aos core library version: " << AOS_CORE_VERSION << std::endl;

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

    mLogger.SetBackend(aos::common::logger::Logger::Backend::eJournald);
}

void App::HandleLogLevel(const std::string& name, const std::string& value)
{
    (void)name;

    aos::LogLevel level;

    auto err = level.FromString(aos::String(value.c_str()));
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
