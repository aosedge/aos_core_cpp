/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_IDENTHANDLER_CERTIDENTIFIERCONFIG_HPP_
#define AOS_IAM_IDENTHANDLER_CERTIDENTIFIERCONFIG_HPP_

#include <core/common/consts.hpp>
#include <core/common/tools/string.hpp>

namespace aos::iam::identhandler {

/**
 * CertIdentifier configuration.
 */
struct CertIdentifierConfig {
    /**
     * Unit model path.
     */
    StaticString<cFilePathLen> mUnitModelPath;

    /**
     * Subjects path.
     */
    StaticString<cFilePathLen> mSubjectsPath;
};

} // namespace aos::iam::identhandler

#endif
