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

#include "app.hpp"
#include "config/config.hpp"
#include "fileidentifier/fileidentifier.hpp"
#include "logger/logmodule.hpp"
// cppcheck-suppress missingInclude
#include "version.hpp"

namespace aos::iam::app {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void ErrorHandler(int sig)
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

void RegisterErrorSignals()
{
    struct sigaction act { };

    act.sa_handler = ErrorHandler;
    act.sa_flags   = SA_RESETHAND;

    sigaction(SIGILL, &act, nullptr);
    sigaction(SIGABRT, &act, nullptr);
    sigaction(SIGFPE, &act, nullptr);
    sigaction(SIGSEGV, &act, nullptr);
}

Error ConvertCertModuleConfig(const config::ModuleConfig& config, certhandler::ModuleConfig& aosConfig)
{
    if (config.mAlgorithm == "ecc") {
        aosConfig.mKeyType = crypto::KeyTypeEnum::eECDSA;
    } else if (config.mAlgorithm == "rsa") {
        aosConfig.mKeyType = crypto::KeyTypeEnum::eRSA;
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
        certhandler::ExtendedKeyUsage keyUsage;

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

    return ErrorEnum::eNone;
}

Error ConvertPKCS11ModuleParams(const config::PKCS11ModuleParams& params, certhandler::PKCS11ModuleConfig& aosParams)
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

    return ErrorEnum::eNone;
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

    RegisterErrorSignals();

    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize logger");

    Application::initialize(self);

    Init();
    Start();

    // Notify systemd

    auto ret = sd_notify(0, cSDNotifyReady);
    if (ret < 0) {
        AOS_ERROR_CHECK_AND_THROW(ret, "can't notify systemd");
    }
}

void App::uninitialize()
{
    Stop();

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

void App::Init()
{
    LOG_INF() << "Initialize IAM: version = " << AOS_CORE_IAM_VERSION;

    // Initialize Aos modules

    auto config = config::ParseConfig(mConfigFile.empty() ? cDefaultConfigFile : mConfigFile);
    AOS_ERROR_CHECK_AND_THROW(config.mError, "can't parse config");

    auto err = mDatabase.Init(config.mValue.mDatabase);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize database");

    err = mNodeInfoProvider.Init(config.mValue.mNodeInfo);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize node info provider");

    err = InitIdentifierModule(config.mValue.mIdentifier);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize identifier module");

    if (config.mValue.mEnablePermissionsHandler) {
        mPermHandler = std::make_unique<permhandler::PermHandler>();
    }

    err = mCryptoProvider.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize crypto provider");

    err = mCertLoader.Init(mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert loader");

    err = InitCertModules(config.mValue);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert modules");

    err = mNodeManager.Init(mDatabase);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize node manager");

    err = mProvisionManager.Init(mIAMServer, mCertHandler);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize provision manager");

    err = mCertProvider.Init(mCertHandler);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert provider");

    err = mIAMServer.Init(config.mValue.mIAMServer, mCertHandler, *mIdentifier, *mPermHandler, mCertLoader,
        mCryptoProvider, mNodeInfoProvider, mNodeManager, mCertProvider, mProvisionManager, mProvisioning);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM server");

    const auto& clientConfig = config.mValue.mIAMClient;
    if (!clientConfig.mMainIAMPublicServerURL.empty() && !clientConfig.mMainIAMProtectedServerURL.empty()) {
        mIAMClient = std::make_unique<iamclient::IAMClient>();

        err = mIAMClient->Init(clientConfig, mIdentifier.get(), mCertProvider, mProvisionManager, mCertLoader,
            mCryptoProvider, mNodeInfoProvider, mProvisioning);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM client");
    }
}

void App::Start()
{
    LOG_INF() << "Start IAM";

    if (mIdentifier) {
        auto err = mIdentifier->Start();
        AOS_ERROR_CHECK_AND_THROW(err, "can't start identifier module");

        mCleanupManager.AddCleanup([this]() {
            if (auto err = mIdentifier->Stop(); !err.IsNone()) {
                LOG_ERR() << "Can't stop identifier module: err=" << err;
            }
        });
    }

    auto err = mIAMServer.Start();
    AOS_ERROR_CHECK_AND_THROW(err, "can't start IAM server");

    mCleanupManager.AddCleanup([this]() {
        if (auto err = mIAMServer.Stop(); !err.IsNone()) {
            LOG_ERR() << "Can't stop IAM server: err=" << err;
        }
    });

    if (mIAMClient) {
        err = mIAMClient->Start();
        AOS_ERROR_CHECK_AND_THROW(err, "can't start IAM client");

        mCleanupManager.AddCleanup([this]() {
            if (auto err = mIAMClient->Stop(); !err.IsNone()) {
                LOG_ERR() << "Can't stop IAM client: err=" << err;
            }
        });
    }
}

void App::Stop()
{
    LOG_INF() << "Stop IAM";

    mCleanupManager.ExecuteCleanups();
}

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

Error App::InitCertModules(const config::Config& config)
{
    LOG_DBG() << "Init cert modules: " << config.mCertModules.size();

    for (const auto& moduleConfig : config.mCertModules) {
        if (moduleConfig.mPlugin != cPKCS11CertModule) {
            return AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument);
        }

        if (moduleConfig.mDisabled) {
            LOG_WRN() << "Skip disabled cert storage: storage = " << moduleConfig.mID.c_str();
            continue;
        }

        auto pkcs11Params = config::ParsePKCS11ModuleParams(moduleConfig.mParams);
        if (!pkcs11Params.mError.IsNone()) {
            return AOS_ERROR_WRAP(pkcs11Params.mError);
        }

        certhandler::ModuleConfig aosConfig {};

        auto err = ConvertCertModuleConfig(moduleConfig, aosConfig);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        certhandler::PKCS11ModuleConfig aosParams {};

        err = ConvertPKCS11ModuleParams(pkcs11Params.mValue, aosParams);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto pkcs11Module = std::make_unique<certhandler::PKCS11Module>();
        auto certModule   = std::make_unique<certhandler::CertModule>();

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

    return ErrorEnum::eNone;
}

Error App::InitIdentifierModule(const config::IdentifierConfig& config)
{
    if (config.mPlugin == "fileidentifier") {
        auto fileIdentifier = std::make_unique<fileidentifier::FileIdentifier>();

        if (auto err = fileIdentifier->Init(config, mIAMServer); !err.IsNone()) {
            return err;
        }

        mIdentifier = std::move(fileIdentifier);
    } else if (config.mPlugin == "visidentifier") {
        auto visIdentifier = std::make_unique<visidentifier::VISIdentifier>();

        if (auto err = visIdentifier->Init(config, mIAMServer); !err.IsNone()) {
            return err;
        }

        mIdentifier = std::move(visIdentifier);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::iam::app
