/*
\ * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_IDENTHANDLER_IDENTHANDLER_HPP_
#define AOS_IAM_IDENTHANDLER_IDENTHANDLER_HPP_

#include <memory>

#include <core/common/crypto/itf/uuid.hpp>
#include <core/iam/identhandler/identmodule.hpp>

#include <iam/config/config.hpp>

namespace aos::iam::identhandler {

/**
 * Creates and initializes identifier module based on the config.
 * Returns nullptr if no module is set in the config.
 *
 * @param config identifier module config.
 * @param uuidProvider UUID provider.
 * @return std::unique_ptr<IdentModuleItf>.
 */
std::unique_ptr<IdentModuleItf> InitializeIdentModule(
    const config::IdentifierConfig& config, crypto::UUIDItf& uuidProvider);

} // namespace aos::iam::identhandler

#endif
