/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WSEXCEPTION_HPP_
#define WSEXCEPTION_HPP_

#include <utils/exception.hpp>

namespace aos::iam::visidentifier {

/**
 * Web socket exception.
 */
class WSException : public aos::common::utils::AosException {
public:
    /**
     * Creates WSException exception instance.
     *
     * @param message exception message.
     * @param err Aos error.
     */
    explicit WSException(const std::string& message, const aos::Error& err = aos::ErrorEnum::eFailed)
        : aos::common::utils::AosException(message, err) {};
};

} // namespace aos::iam::visidentifier

#endif
