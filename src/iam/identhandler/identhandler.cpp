/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <core/iam/identhandler/identmodules/fileidentifier/fileidentifier.hpp>

#include "certidentifier.hpp"
#include "identhandler.hpp"
// TODO: Disabled due to crash on shutdown (see eVISIdentifier case below)
// #include "visidentifier/visidentifier.hpp"

namespace aos::iam::identhandler {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class IdentifierModuleType {
public:
    enum class Enum {
        eFileIdentifier,
        eCertIdentifier,
        eVISIdentifier,
        eNone,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sStrings[] = {
            "fileidentifier",
            "certidentifier",
            "visidentifier",
            "none",
        };

        return Array<const char* const>(sStrings, ArraySize(sStrings));
    };
};

using IdentifierModuleEnum = IdentifierModuleType::Enum;
using IdentifierModule     = EnumStringer<IdentifierModuleType>;

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

std::unique_ptr<IdentModuleItf> InitializeIdentModule(const config::IdentifierConfig& config,
    [[maybe_unused]] crypto::UUIDItf& uuidProvider, iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, AllocatorItf& allocator)
{
    const std::string& plugin = config.mPlugin.empty() ? "none" : config.mPlugin;

    IdentifierModule module;

    auto err = module.FromString(plugin.c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "unknown identifier module plugin");

    switch (module.GetValue()) {
    case IdentifierModuleEnum::eFileIdentifier: {
        auto identifier = std::make_unique<FileIdentifier>();

        auto params = std::make_unique<FileIdentifierConfig>();

        err = config::ParseFileIdentifierModuleParams(config.mParams, *params);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = identifier->Init(*params);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize file identifier module");

        return identifier;
    }

    case IdentifierModuleEnum::eCertIdentifier: {
        auto identifier = std::make_unique<CertIdentifier>();

        auto params = std::make_unique<CertIdentifierConfig>();

        err = config::ParseCertIdentifierModuleParams(config.mParams, *params);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = identifier->Init(*params, certProvider, certLoader, allocator);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize cert identifier module");

        return identifier;
    }

        // TODO: VISIdentifier is disabled due to application crash on shutdown.
        // Crash occurs in Poco::MemoryPool::clear() during static destruction (__cxa_finalize).
        // This is possibly a conflict between gRPC experimental TLS API (TlsServerCredentials)
        // and Poco::Net static objects. When VISIdentifier code is linked (even without being
        // executed), Poco::Net library loads with its static MemoryPool objects, which may
        // get corrupted during gRPC TLS cleanup.
        //
        //
        // case IdentifierModuleEnum::eVISIdentifier: {
        //     auto identifier = std::make_unique<aos::iam::visidentifier::VISIdentifier>();
        //
        //     err = identifier->Init(config, uuidProvider);
        //     AOS_ERROR_CHECK_AND_THROW(err, "can't initialize VIS identifier module");
        //
        //     return identifier;
        // }

    default:
        return nullptr;
    }
}

} // namespace aos::iam::identhandler
