/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sstream>

#include <iostream>

#include "utils/exception.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

AosException::AosException(const std::string& message, const Error& err)
    : Poco::Exception(message, err.Message(), err.Errno())
    , mError(err, message.c_str())
{
    std::stringstream ss;

    ss << message;

    StaticString<cMaxErrorStrLen> errStr;

    if (errStr.Convert(err).IsNone()) {
        ss << ": " << errStr.CStr();
    }

    Poco::Exception::message(ss.str());
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
