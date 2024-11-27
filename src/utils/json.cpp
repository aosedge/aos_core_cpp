/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include "utils/json.hpp"

namespace aos::common::utils {

CaseInsensitiveObjectWrapper::CaseInsensitiveObjectWrapper(const Poco::JSON::Object::Ptr& object)
    : mObject(object)
{
    for (const auto& pair : *object) {
        mKeyMap.emplace(ToLowercase(pair.first), pair.first);
    }
}

CaseInsensitiveObjectWrapper::CaseInsensitiveObjectWrapper(const Poco::Dynamic::Var& var)
    : CaseInsensitiveObjectWrapper(var.extract<Poco::JSON::Object::Ptr>())
{
}

bool CaseInsensitiveObjectWrapper::Has(const std::string& key) const
{
    std::string lowerKey = ToLowercase(key);

    return mKeyMap.count(lowerKey) > 0;
}

Poco::Dynamic::Var CaseInsensitiveObjectWrapper::Get(const std::string& key) const
{
    std::string lowerKey = ToLowercase(key);
    auto        it       = mKeyMap.find(lowerKey);

    if (it == mKeyMap.end()) {
        throw Poco::NotFoundException("Key not found");
    }

    return mObject->get(it->second);
}

Poco::JSON::Array::Ptr CaseInsensitiveObjectWrapper::GetArray(const std::string& key) const
{
    return Get(key).extract<Poco::JSON::Array::Ptr>();
}

CaseInsensitiveObjectWrapper::operator Poco::JSON::Object::Ptr() const
{
    return mObject;
}

CaseInsensitiveObjectWrapper CaseInsensitiveObjectWrapper::GetObject(const std::string& key) const
{
    Poco::Dynamic::Var value = Get(key);

    return CaseInsensitiveObjectWrapper(value.extract<Poco::JSON::Object::Ptr>());
}

std::string CaseInsensitiveObjectWrapper::ToLowercase(const std::string& str) const
{
    std::string lowerStr = str;

    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);

    return lowerStr;
}

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

Error WriteJsonToFile(const Poco::JSON::Object::Ptr& json, const std::string& path)
{
    std::ofstream file(path);
    if (!file.is_open()) {
        return Error(ErrorEnum::eFailed, "Failed to open file");
    }

    try {
        Poco::JSON::Stringifier::stringify(json, file);
    } catch (const std::exception& e) {
        return Error(ErrorEnum::eFailed, e.what());
    }

    return ErrorEnum::eNone;
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

} // namespace aos::common::utils
