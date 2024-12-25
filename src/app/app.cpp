/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <csignal>
#include <execinfo.h>
#include <iostream>

#include <Poco/SignalHandler.h>
#include <Poco/Util/HelpFormatter.h>
#include <systemd/sd-daemon.h>

#include <aos/common/version.hpp>
#include <utils/exception.hpp>

#include "config/config.hpp"
#include "logger/logmodule.hpp"
#include "version.hpp" // cppcheck-suppress missingInclude

#include "app.hpp"

namespace aos::sm::app {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

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

    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize logger", err);

    Application::initialize(self);

    LOG_INF() << "Initialize SM: version = " << AOS_CORE_SM_VERSION;

    // Initialize Aos modules

    auto config = std::make_shared<config::Config>();

    Tie(*config, err) = config::ParseConfig(mConfigFile.empty() ? cDefaultConfigFile : mConfigFile);
    AOS_ERROR_CHECK_AND_THROW("can't parse config", err);

    // Initialize crypto provider

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize crypto provider", err);

    // Initialize cert loader

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW("can't initialize cert loader", err);

    // Initialize IAM client

    auto iamConfig = std::make_unique<common::iamclient::Config>();

    iamConfig->mCACert             = config->mCACert;
    iamConfig->mIAMPublicServerURL = config->mIAMPublicServerURL;

    err = mIAMClientPublic.Init(*iamConfig, mCertLoader, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW("can't initialize public IAM client", err);

    auto nodeInfo = std::make_shared<NodeInfo>();

    err = mIAMClientPublic.GetNodeInfo(*nodeInfo);
    AOS_ERROR_CHECK_AND_THROW("can't get node info", err);

    // Initialize host device manager

    err = mHostDeviceManager.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize host device manager", err);

    // Initialize resource manager

    err = mResourceManager.Init(
        mJSONProvider, mHostDeviceManager, nodeInfo->mNodeType, config->mNodeConfigFile.c_str());
    AOS_ERROR_CHECK_AND_THROW("can't initialize resource manager", err);

    // Initialize database

    err = mDatabase.Init(config->mWorkingDir, config->mMigration);
    AOS_ERROR_CHECK_AND_THROW("can't initialize database", err);

    // Initialize network manager

    err = mNetworkManager.Init(
        mDatabase, mCNI, mTrafficMonitor, mNamespaceManager, mNetworkInterfaceManager, config->mWorkingDir.c_str());
    AOS_ERROR_CHECK_AND_THROW("can't initialize network manager", err);

    // Initialize resource monitor

    err = mResourceMonitor.Init(mIAMClientPublic, mResourceUsageProvider, mSMClient, mSMClient);
    AOS_ERROR_CHECK_AND_THROW("can't initialize resource monitor", err);

    // Initialize service manager

    auto serviceManagerConfig = std::make_shared<sm::servicemanager::Config>();

    serviceManagerConfig->mServicesDir = config->mServicesDir.c_str();
    serviceManagerConfig->mDownloadDir = config->mDownloadDir.c_str();
    serviceManagerConfig->mTTL         = config->mServiceTTL.count();

    err = mServiceManager.Init(*serviceManagerConfig, mOCISpec, mDownloader, mDatabase, mServicesSpaceAllocator,
        mDownloadSpaceAllocator, mImageHandler);

    // Notify systemd

    auto ret = sd_notify(0, cSDNotifyReady);
    if (ret < 0) {
        AOS_ERROR_CHECK_AND_THROW("can't notify systemd", ret);
    }
}

void App::uninitialize()
{
    Application::uninitialize();
}

void App::reinitialize(Application& self)
{
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
    helpFormatter.setHeader("Aos SM manager service.");
    helpFormatter.format(std::cout);

    stopOptionsProcessing();
}

void App::HandleVersion(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mStopProcessing = true;

    std::cout << "Aos IA manager version:   " << AOS_CORE_SM_VERSION << std::endl;
    std::cout << "Aos core library version: " << AOS_CORE_VERSION << std::endl;

    stopOptionsProcessing();
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

} // namespace aos::sm::app
