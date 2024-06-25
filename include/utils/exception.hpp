/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXCEPTION_HPP_
#define EXCEPTION_HPP_

#include <sstream>

#include <Poco/Exception.h>

#include <aos/common/tools/error.hpp>
#include <aos/common/tools/string.hpp>

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
     * @param err Aos error.
     */
    explicit AosException(const std::string& message, const aos::Error& err = aos::ErrorEnum::eFailed)
        : Poco::Exception(message, err.Message(), err.Errno())
        , mError(err)
    {
        std::stringstream ss;

        ss << message;

        StaticString<cMaxErrorStrLen> errStr;

        if (errStr.Convert(err).IsNone()) {
            ss << ": " << errStr.CStr();
        }

        Poco::Exception::message(ss.str());
    };

    /**
     * Returns Aos error.
     *
     * @return aos::Error.
     */
    aos::Error GetError() const { return mError; }

    /**
     * Returns a static string describing the exception.
     *
     * @return const char*
     */
    const char* what() const noexcept override { return mError.Message(); }

private:
    aos::Error mError;
};

} // namespace aos::common::utils

#endif
