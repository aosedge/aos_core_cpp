/*
\ * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <core/iam/identhandler/identmodules/fileidentifier/fileidentifier.hpp>

#include "identhandler.hpp"
#include "visidentifier/visidentifier.hpp"

namespace aos::iam::identhandler {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

std::unique_ptr<IdentModuleItf> InitializeIdentModule(
    const config::IdentifierConfig& config, crypto::UUIDItf& uuidProvider)
{
    const std::string& plugin = config.mPlugin;

    if (plugin == "fileidentifier") {
        auto identifier = std::make_unique<FileIdentifier>();

        auto params = std::make_unique<FileIdentifierConfig>();

        auto err = config::ParseFileIdentifierModuleParams(config.mParams, *params);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = identifier->Init(*params);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize file identifier module");

        return identifier;
    } else if (plugin == "visidentifier") {
        auto identifier = std::make_unique<aos::iam::visidentifier::VISIdentifier>();

        auto err = identifier->Init(config, uuidProvider);
        AOS_ERROR_CHECK_AND_THROW(err, "can't initialize VIS identifier module");

        return identifier;
    }

    return nullptr;
}

} // namespace aos::iam::identhandler
