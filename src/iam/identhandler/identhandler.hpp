/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_IDENTHANDLER_IDENTHANDLER_HPP_
#define AOS_IAM_IDENTHANDLER_IDENTHANDLER_HPP_

#include <memory>

#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/crypto/itf/uuid.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/tools/memory.hpp>
#include <core/iam/identhandler/itf/identmodule.hpp>

#include <iam/config/config.hpp>

namespace aos::iam::identhandler {

/**
 * Creates and initializes identifier module based on the config.
 * Returns nullptr if no module is set in the config.
 *
 * @param config identifier module config.
 * @param uuidProvider UUID provider.
 * @param certProvider certificate provider.
 * @param certLoader certificate loader.
 * @param allocator allocator.
 * @return std::unique_ptr<IdentModuleItf>.
 */
std::unique_ptr<IdentModuleItf> InitializeIdentModule(const config::IdentifierConfig& config,
    crypto::UUIDItf& uuidProvider, iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    AllocatorItf& allocator);

} // namespace aos::iam::identhandler

#endif
