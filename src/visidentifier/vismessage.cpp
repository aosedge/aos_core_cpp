/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Stringifier.h>

#include <utils/exception.hpp>
#include <utils/json.hpp>

#include "vismessage.hpp"

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

VISMessage::VISMessage(const VISAction action)
    : mAction(action)
{
    mJsonObject.set(cActionTagName, mAction.ToString().CStr());
}

VISMessage::VISMessage(const VISAction action, const std::string& requestId, const std::string& path)
    : VISMessage(action)
{
    if (!requestId.empty()) {
        mJsonObject.set(cRequestIdTagName, requestId);
    }

    if (!path.empty()) {
        mJsonObject.set(cPathTagName, path);
    }
}

VISMessage::VISMessage(const std::string& jsonStr)
{
    try {
        Poco::Dynamic::Var objectVar;
        aos::Error         err;

        aos::Tie(objectVar, err) = aos::common::utils::ParseJson(jsonStr);
        AOS_ERROR_CHECK_AND_THROW("can't parse as json", err);

        mJsonObject = std::move(*objectVar.extract<JsonObject::Ptr>());

        mAction.FromString(mJsonObject.getValue<std::string>(cActionTagName).c_str());
    } catch (const Poco::Exception& e) {
        throw aos::common::utils::AosException(e.message(), AOS_ERROR_WRAP(aos::ErrorEnum::eFailed));
    }
}

bool VISMessage::Is(const VISAction actionType) const
{
    return mAction == actionType;
}

const JsonObject& VISMessage::GetJSON() const
{
    return mJsonObject;
}

std::string VISMessage::ToString() const
{
    std::ostringstream jsonStream;
    Poco::JSON::Stringifier::stringify(mJsonObject, jsonStream);

    return jsonStream.str();
}

std::vector<uint8_t> VISMessage::ToByteArray() const
{
    const auto str = ToString();

    return {str.cbegin(), str.cend()};
}
