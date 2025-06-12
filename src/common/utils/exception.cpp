/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <iostream>

#include "exception.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

AosException::AosException(const Error& err, const std::string& message)
    : Poco::Exception(message, err.Message(), err.Errno())
    , mError(err, message.empty() ? nullptr : message.c_str())
{
    std::string finalMessage;

    if (!message.empty()) {
        finalMessage = message;

        StaticString<cMaxErrorStrLen> errStr;
        if (errStr.Convert(err).IsNone()) {
            finalMessage += ": " + std::string(errStr.CStr());
        }
    } else {
        StaticString<cMaxErrorStrLen> errStr;
        if (errStr.Convert(err).IsNone()) {
            finalMessage = errStr.CStr();
        }
    }

    Poco::Exception::message(finalMessage);
}

Error ToAosError(const std::exception& e, ErrorEnum err)
{
    if (const auto* aosExc = dynamic_cast<const AosException*>(&e)) {
        return aosExc->GetError();
    }

    if (const auto* pocoExc = dynamic_cast<const Poco::Exception*>(&e)) {
        return Error {err, pocoExc->displayText().c_str()};
    }

    return Error {err, e.what()};
}

} // namespace aos::common::utils
