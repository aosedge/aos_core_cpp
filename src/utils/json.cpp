/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/JSONException.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include "utils/json.hpp"

namespace UtilsJson {

aos::RetWithError<Poco::Dynamic::Var> ParseJson(const std::string& json) noexcept
{
    try {
        auto parser = Poco::JSON::Parser();

        return parser.parse(json);
    } catch (const Poco::JSON::JSONException& e) {
        return {{}, aos::ErrorEnum::eInvalidArgument};
    } catch (...) {
        return {{}, aos::ErrorEnum::eFailed};
    }
}

aos::RetWithError<Poco::Dynamic::Var> ParseJson(std::istream& in) noexcept
{
    try {
        auto parser = Poco::JSON::Parser();

        return parser.parse(in);
    } catch (const Poco::JSON::JSONException& e) {
        return {{}, aos::ErrorEnum::eInvalidArgument};
    } catch (...) {
        return {{}, aos::ErrorEnum::eFailed};
    }
}

Poco::Dynamic::Var FindByPath(const Poco::Dynamic::Var object, const std::vector<std::string>& keys)
{
    if (keys.empty()) {
        return object;
    }

    Poco::Dynamic::Var result = object;

    for (const auto& key : keys) {

        if (result.type() == typeid(Poco::JSON::Object)) {
            result = result.extract<Poco::JSON::Object>().get(key);
        } else if (result.type() == typeid(Poco::JSON::Object::Ptr)) {
            result = result.extract<Poco::JSON::Object::Ptr>()->get(key);
        } else {
            result.clear();

            break;
        }
    }

    return result;
}

} // namespace UtilsJson
