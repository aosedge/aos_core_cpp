/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "utils/json.hpp"

namespace aos::common::utils {

CaseInsensitiveObjectWrapper::CaseInsensitiveObjectWrapper(const Poco::JSON::Object::Ptr& object)
    : mObject(object)
{
    for (const auto& pair : *object) {
        mKeyMap.emplace(ToLowercase(pair.first), pair.first);
    }
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
