/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
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
#include <aos/iam/certmodules/certmodule.hpp>
#include <utils/exception.hpp>

#include "config/config.hpp"

#include "app.hpp"
#include "logger/logmodule.hpp"
// cppcheck-suppress missingInclude
#include "version.hpp"

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static void ErrorHandler(int sig)
{
    static constexpr auto cBacktraceSize = 32;

    void*  array[cBacktraceSize];
    size_t size;

    switch (sig) {
    case SIGILL:
        std::cerr << "Illegal instruction" << std::endl;
        break;

    case SIGABRT:
        std::cerr << "Aborted" << std::endl;
        break;

    case SIGFPE:
        std::cerr << "Floating point exception" << std::endl;
        break;

    case SIGSEGV:
        std::cerr << "Segmentation fault" << std::endl;
        break;

    default:
        std::cerr << "Unknown signal" << std::endl;
        break;
    }

    size = backtrace(array, cBacktraceSize);

    backtrace_symbols_fd(array, size, STDERR_FILENO);

    raise(sig);
}

static void RegisterErrorSignals()
{
    struct sigaction act { };

    act.sa_handler = ErrorHandler;
    act.sa_flags   = SA_RESETHAND;

    sigaction(SIGILL, &act, nullptr);
    sigaction(SIGABRT, &act, nullptr);
    sigaction(SIGFPE, &act, nullptr);
    sigaction(SIGSEGV, &act, nullptr);
}

static aos::Error ConvertCertModuleConfig(
    const aos::iam::config::ModuleConfig& config, aos::iam::certhandler::ModuleConfig& aosConfig)
{
    if (config.mAlgorithm == "ecc") {
        aosConfig.mKeyType = aos::crypto::KeyTypeEnum::eECDSA;
    } else if (config.mAlgorithm == "rsa") {
        aosConfig.mKeyType = aos::crypto::KeyTypeEnum::eRSA;
    } else {
        auto err = aosConfig.mKeyType.FromString(config.mAlgorithm.c_str());
        if (!err.IsNone()) {
            return err;
        }
    }

    aosConfig.mMaxCertificates = config.mMaxItems;
    aosConfig.mSkipValidation  = config.mSkipValidation;
    aosConfig.mIsSelfSigned    = config.mIsSelfSigned;

    for (auto const& keyUsageStr : config.mExtendedKeyUsage) {
        aos::iam::certhandler::ExtendedKeyUsage keyUsage;

        auto err = keyUsage.FromString(keyUsageStr.c_str());
        if (!err.IsNone()) {
            return err;
        }

        err = aosConfig.mExtendedKeyUsage.PushBack(keyUsage);
        if (!err.IsNone()) {
            return err;
        }
    }

    for (auto const& nameStr : config.mAlternativeNames) {
        auto err = aosConfig.mAlternativeNames.EmplaceBack(nameStr.c_str());
        if (!err.IsNone()) {
            return err;
        }
    }

    return aos::ErrorEnum::eNone;
}

static aos::Error ConvertPKCS11ModuleParams(
    const aos::iam::config::PKCS11ModuleParams& params, aos::iam::certhandler::PKCS11ModuleConfig& aosParams)
{
    aosParams.mLibrary = params.mLibrary.c_str();

    if (params.mSlotID.has_value()) {
        aosParams.mSlotID.EmplaceValue(params.mSlotID.value());
    }

    if (params.mSlotIndex.has_value()) {
        aosParams.mSlotIndex.EmplaceValue(params.mSlotIndex.value());
    }

    aosParams.mTokenLabel      = params.mTokenLabel.c_str();
    aosParams.mUserPINPath     = params.mUserPINPath.c_str();
    aosParams.mModulePathInURL = params.mModulePathInURL;
    aosParams.mUID             = params.mUID;
    aosParams.mGID             = params.mGID;

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

void App::initialize(Application& self)
{
    if (mStopProcessing) {
        return;
    }

    RegisterErrorSignals();

    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize logger", err);

    Application::initialize(self);

    LOG_INF() << "Initialize IAM: version = " << AOS_CORE_IAM_VERSION;

    // Initialize Aos modules

    auto config = aos::iam::config::ParseConfig(mConfigFile.empty() ? cDefaultConfigFile : mConfigFile);
    AOS_ERROR_CHECK_AND_THROW("can't parse config", config.mError);

    err = mDatabase.Init(config.mValue.mWorkingDir, config.mValue.mMigration);
    AOS_ERROR_CHECK_AND_THROW("can't initialize database", err);

    err = mNodeInfoProvider.Init(config.mValue.mNodeInfo);
    AOS_ERROR_CHECK_AND_THROW("can't initialize node info provider", err);

    if (!config.mValue.mIdentifier.mPlugin.empty()) {
        auto visIdentifier = std::make_unique<aos::iam::visidentifier::VISIdentifier>();

        err = visIdentifier->Init(config.mValue, mIAMServer);
        AOS_ERROR_CHECK_AND_THROW("can't initialize VIS identifier", err);

        mIdentifier = std::move(visIdentifier);
    }

    if (config.mValue.mEnablePermissionsHandler) {
        mPermHandler = std::make_unique<aos::iam::permhandler::PermHandler>();
    }

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize crypto provider", err);

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW("can't initialize cert loader", err);

    err = InitCertModules(config.mValue);
    AOS_ERROR_CHECK_AND_THROW("can't initialize cert modules", err);

    err = mNodeManager.Init(mDatabase);
    AOS_ERROR_CHECK_AND_THROW("can't initialize node manager", err);

    err = mProvisionManager.Init(mIAMServer, mCertHandler);
    AOS_ERROR_CHECK_AND_THROW("can't initialize provision manager", err);

    err = mCertProvider.Init(mCertHandler);
    AOS_ERROR_CHECK_AND_THROW("can't initialize cert provider", err);

    err = mIAMServer.Init(config.mValue, mCertHandler, *mIdentifier, *mPermHandler, mCertLoader, mCryptoProvider,
        mNodeInfoProvider, mNodeManager, mCertProvider, mProvisionManager, mProvisioning);
    AOS_ERROR_CHECK_AND_THROW("can't initialize IAM server", err);

    if (!config.mValue.mMainIAMPublicServerURL.empty() && !config.mValue.mMainIAMProtectedServerURL.empty()) {
        mIAMClient = std::make_unique<aos::iam::iamclient::IAMClient>();

        err = mIAMClient->Init(config.mValue, mIdentifier.get(), mCertProvider, mProvisionManager, mCertLoader,
            mCryptoProvider, mNodeInfoProvider, mProvisioning);
        AOS_ERROR_CHECK_AND_THROW("can't initialize IAM client", err);
    }

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

    std::cout << "Aos IA manager version:   " << AOS_CORE_IAM_VERSION << std::endl;
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

aos::Error App::InitCertModules(const aos::iam::config::Config& config)
{
    LOG_DBG() << "Init cert modules: " << config.mCertModules.size();

    for (const auto& moduleConfig : config.mCertModules) {
        if (moduleConfig.mPlugin != cPKCS11CertModule) {
            return AOS_ERROR_WRAP(aos::ErrorEnum::eInvalidArgument);
        }

        if (moduleConfig.mDisabled) {
            LOG_WRN() << "Skip disabled cert storage: storage = " << moduleConfig.mID.c_str();
            continue;
        }

        auto pkcs11Params = aos::iam::config::ParsePKCS11ModuleParams(moduleConfig.mParams);
        if (!pkcs11Params.mError.IsNone()) {
            return AOS_ERROR_WRAP(pkcs11Params.mError);
        }

        aos::iam::certhandler::ModuleConfig aosConfig {};

        auto err = ConvertCertModuleConfig(moduleConfig, aosConfig);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        aos::iam::certhandler::PKCS11ModuleConfig aosParams {};

        err = ConvertPKCS11ModuleParams(pkcs11Params.mValue, aosParams);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto pkcs11Module = std::make_unique<aos::iam::certhandler::PKCS11Module>();
        auto certModule   = std::make_unique<aos::iam::certhandler::CertModule>();

        err = pkcs11Module->Init(moduleConfig.mID.c_str(), aosParams, mPKCS11Manager, mCryptoProvider);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        err = certModule->Init(moduleConfig.mID.c_str(), aosConfig, mCryptoProvider, *pkcs11Module, mDatabase);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        LOG_DBG() << "Register cert module: " << certModule->GetCertType();

        err = mCertHandler.RegisterModule(*certModule);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mCertModules.emplace_back(std::make_pair(std::move(pkcs11Module), std::move(certModule)));
    }

    return aos::ErrorEnum::eNone;
}
