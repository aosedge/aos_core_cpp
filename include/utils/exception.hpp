/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXCEPTION_HPP_
#define EXCEPTION_HPP_

#include <Poco/Exception.h>

#include <aos/common/tools/error.hpp>
#include <aos/common/tools/string.hpp>

#include <aos/common/tools/log.hpp>

/**
 * Throws exception with Aos error and specified message.
 */
#define AOS_ERROR_THROW(message, err) throw aos::common::utils::AosException(message, AOS_ERROR_WRAP(err))

/**
 * Checks Aos error and throws exception if error is not none.
 */
#define AOS_ERROR_CHECK_AND_THROW(message, err)                                                                        \
    if (!aos::Error(err).IsNone()) {                                                                                   \
        AOS_ERROR_THROW(message, err);                                                                                 \
    }

namespace aos::common::utils {

/**
 * Aos exception.
 */
class AosException : public Poco::Exception {
public:
    /**
     * Creates Aos exception instance.
     *
     * @param message message.
     * @param err Aos error.
     */
    explicit AosException(const std::string& message, const Error& err = ErrorEnum::eFailed);

    /**
     * Returns Aos error.
     *
     * @return Error.
     */
    Error GetError() const { return mError; }

    /**
     * Returns a static string describing the exception.
     *
     * @return const char*
     */
    const char* name() const noexcept override { return "Aos exception"; }

private:
    Error mError;
};

/**
 * Converts exception to Aos error.
 *
 * @param e exception.
 * @param err error.
 *
 * @return Error.
 */
Error ToAosError(const std::exception& e, ErrorEnum err = ErrorEnum::eFailed);

} // namespace aos::common::utils

#endif
