/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>
#include <core/common/version/version.hpp>
#include <core/iam/certhandler/certmodule.hpp>

#include <common/utils/exception.hpp>
#include <common/version/version.hpp>
#include <iam/config/config.hpp>
#include <iam/identhandler/identhandler.hpp>

#include "aoscore.hpp"

namespace aos::iam::app {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

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
 * Public
 **********************************************************************************************************************/

AosCore::AosCore()
    : mCertHandler(mAllocator)
{
}

void AosCore::Init(const std::string& configFile, bool provisioning)
{
    mProvisioning = provisioning;

    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize logger");

    LOG_INF() << "Init IAM" << Log::Field("version", AOS_CORE_CPP_VERSION);
    LOG_DBG() << "Aos core size" << Log::Field("size", sizeof(AosCore));

    // Initialize Aos modules

    auto config = config::ParseConfig(configFile.empty() ? cDefaultConfigFile : configFile);
    AOS_ERROR_CHECK_AND_THROW(config.mError, "can't parse config");

    err = mDatabase.Init(config.mValue.mDatabase);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize database");

    err = mCurrentNodeHandler.Init(config.mValue.mNodeInfo);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize current node handler");

    err = InitIdentifierModule(config.mValue.mIdentifier);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize identifier module");

    err = mCryptoProvider.Init(mAllocator);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize crypto provider");

    err = mPKCS11Manager.Init(mAllocator);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize PKCS11 manager");

    err = mCertLoader.Init(mAllocator, mCryptoProvider, mPKCS11Manager);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert loader");

    err = mTLSCredentials.Init(mCertHandler, mCertLoader, mCryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize TLS credentials");

    err = InitCertModules(config.mValue);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert modules");

    if (config.mValue.mEnablePermissionsHandler) {
        mPermHandler = std::make_unique<permhandler::PermHandler>();

        err = mPermHandler->Init(mCryptoProvider);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize permissions handler");
    }

    err = mNodeManager.Init(mAllocator, mDatabase);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize node manager");

    err = mProvisionManager.Init(mIAMServer, mCertHandler);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize provision manager");

    err = mIAMServer.Init(config.mValue.mIAMServer, mCertHandler, *mIdentifier, *mPermHandler, mCertLoader,
        mCryptoProvider, mCurrentNodeHandler, mNodeManager, mCertHandler, mProvisionManager, mProvisioning);
    AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM server");

    const auto& clientConfig = config.mValue.mIAMClient;
    if (!clientConfig.mMainIAMPublicServerURL.empty() && !clientConfig.mMainIAMProtectedServerURL.empty()) {
        mIAMClient = std::make_unique<iamclient::IAMClient>();

        err = mIAMClient->Init(clientConfig, mIdentifier.get(), mCertHandler, mProvisionManager, mTLSCredentials,
            mCurrentNodeHandler, mProvisioning);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize IAM client");
    }
}

void AosCore::Start()
{
    LOG_INF() << "Start IAM" << Log::Field("provisioning", mProvisioning);

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

void AosCore::Stop()
{
    LOG_INF() << "Stop IAM";

    mCleanupManager.ExecuteCleanups();
}

void AosCore::SetLogBackend(common::logger::Logger::Backend backend)
{
    mLogger.SetBackend(backend);
}

void AosCore::SetLogLevel(LogLevel level)
{
    mLogger.SetLogLevel(level);
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error AosCore::InitCertModules(const config::Config& config)
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

        err = pkcs11Module->Init(mAllocator, moduleConfig.mID.c_str(), aosParams, mPKCS11Manager, mCryptoProvider);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        err = certModule->Init(
            mAllocator, moduleConfig.mID.c_str(), aosConfig, mCryptoProvider, *pkcs11Module, mDatabase);
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

Error AosCore::InitIdentifierModule(const config::IdentifierConfig& config)
{
    mIdentifier = identhandler::InitializeIdentModule(config, mCryptoProvider);

    if (mIdentifier) {
        mIdentifier->SubscribeListener(mIAMServer);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::iam::app
