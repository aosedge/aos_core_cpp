/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_APP_AOSCORE_HPP_
#define AOS_IAM_APP_AOSCORE_HPP_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <core/common/crypto/certloader.hpp>
#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tools/heapallocator.hpp>
#include <core/iam/certhandler/certmodules/pkcs11/pkcs11.hpp>
#include <core/iam/identhandler/itf/identmodule.hpp>
#include <core/iam/nodemanager/nodemanager.hpp>
#include <core/iam/permhandler/permhandler.hpp>
#include <core/iam/provisionmanager/provisionmanager.hpp>

#include <common/iamclient/tlscredentials.hpp>
#include <common/logger/logger.hpp>
#include <common/utils/cleanupmanager.hpp>
#include <iam/currentnode/currentnodehandler.hpp>
#include <iam/database/database.hpp>
#include <iam/iamclient/iamclient.hpp>
#include <iam/iamserver/iamserver.hpp>

namespace aos::iam::app {

/**
 * Aos core instance.
 */
class AosCore {
public:
    /**
     * Creates a new object instance.
     */
    AosCore();

    /**
     * Initializes Aos core.
     *
     * @param configFile config file path.
     * @param provisioning provisioning mode flag.
     */
    void Init(const std::string& configFile, bool provisioning);

    /**
     * Starts Aos core.
     */
    void Start();

    /**
     * Stops Aos core.
     */
    void Stop();

    /**
     * Sets log backend.
     *
     * @param backend log backend.
     */
    void SetLogBackend(aos::common::logger::Logger::Backend backend);

    /**
     * Sets log level.
     *
     * @param level log level.
     */
    void SetLogLevel(aos::LogLevel level);

private:
    Error InitCertModules(const config::Config& config);
    Error InitIdentifierModule(const config::IdentifierConfig& config);

    static constexpr auto cDefaultConfigFile = "aos_iamanager.cfg";
    static constexpr auto cPKCS11CertModule  = "pkcs11module";

    aos::HeapAllocator mAllocator;

    crypto::DefaultCryptoProvider mCryptoProvider;
    crypto::CertLoader            mCertLoader;
    certhandler::CertHandler      mCertHandler;
    pkcs11::PKCS11Manager         mPKCS11Manager;
    std::vector<std::pair<std::unique_ptr<certhandler::HSMItf>, std::unique_ptr<certhandler::CertModule>>> mCertModules;
    database::Database                                                                                     mDatabase;
    currentnode::CurrentNodeHandler               mCurrentNodeHandler;
    nodemanager::NodeManager                      mNodeManager;
    provisionmanager::ProvisionManager            mProvisionManager;
    iamserver::IAMServer                          mIAMServer;
    common::iamclient::TLSCredentials             mTLSCredentials;
    common::logger::Logger                        mLogger;
    std::unique_ptr<permhandler::PermHandler>     mPermHandler;
    std::unique_ptr<iamclient::IAMClient>         mIAMClient;
    std::unique_ptr<identhandler::IdentModuleItf> mIdentifier;
    aos::common::utils::CleanupManager            mCleanupManager;

    bool mProvisioning = false;
};

} // namespace aos::iam::app

#endif
