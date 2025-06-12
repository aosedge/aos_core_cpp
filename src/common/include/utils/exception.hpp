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
 * Helper macros for argument counting
 */
#define _GET_NTH_ARG(_1, _2, NAME, ...) NAME
#define GET_MACRO(NAME)                 NAME

/**
 * Error throw with and without message
 */
#define AOS_ERROR_THROW_1(err)          throw aos::common::utils::AosException(AOS_ERROR_WRAP(err))
#define AOS_ERROR_THROW_2(err, message) throw aos::common::utils::AosException(AOS_ERROR_WRAP(err), message)
#define AOS_ERROR_THROW(...)            GET_MACRO(_GET_NTH_ARG(__VA_ARGS__, AOS_ERROR_THROW_2, AOS_ERROR_THROW_1))(__VA_ARGS__)

/**
 * Error check and throw with and without message
 */
#define AOS_ERROR_CHECK_AND_THROW_1(err)                                                                               \
    if (!aos::Error(err).IsNone()) {                                                                                   \
        AOS_ERROR_THROW_1(err);                                                                                        \
    }
#define AOS_ERROR_CHECK_AND_THROW_2(err, message)                                                                      \
    if (!aos::Error(err).IsNone()) {                                                                                   \
        AOS_ERROR_THROW_2(err, message);                                                                               \
    }
#define AOS_ERROR_CHECK_AND_THROW(...)                                                                                 \
    GET_MACRO(_GET_NTH_ARG(__VA_ARGS__, AOS_ERROR_CHECK_AND_THROW_2, AOS_ERROR_CHECK_AND_THROW_1))(__VA_ARGS__)

namespace aos::common::utils {

/**
 * Aos exception.
 */
class AosException : public Poco::Exception {
public:
    /**
     * Creates Aos exception instance.
     *
     * @param err Aos error.
     * @param message message.
     */
    explicit AosException(const Error& err, const std::string& message = "");

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
