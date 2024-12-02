/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UTILS_JSON_HPP_
#define UTILS_JSON_HPP_

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Object.h>

#include <aos/common/tools/error.hpp>

namespace aos::common::utils {
/**
 * Parses json string.
 *
 * @param json json string.
 * @return aos::RetWithError<Poco::Dynamic::Var> .
 */
aos::RetWithError<Poco::Dynamic::Var> ParseJson(const std::string& json) noexcept;

/**
 * Parses input stream.
 *
 * @param in input stream.
 * @return aos::RetWithError<Poco::Dynamic::Var> .
 */
aos::RetWithError<Poco::Dynamic::Var> ParseJson(std::istream& in) noexcept;

/**
 * Writes json to file.
 *
 * @param json json object.
 * @param path path to the file.
 * @return aos::Error.
 */
Error WriteJsonToFile(const Poco::JSON::Object::Ptr& json, const std::string& path);

/**
 * Finds value of the json by path
 *
 * @param object json object.
 * @param path json path.
 * @return Poco::Dynamic::Var.
 */
Poco::Dynamic::Var FindByPath(const Poco::Dynamic::Var object, const std::vector<std::string>& path);

/**
 * Wrapper for Poco::JSON::Object::Ptr with case-insensitive keys.
 */
class CaseInsensitiveObjectWrapper {
public:
    /**
     * Constructor.
     *
     * @param object json object.
     */
    explicit CaseInsensitiveObjectWrapper(const Poco::JSON::Object::Ptr& object);

    /**
     * Constructor from poco dynamic variable. Throws exception json object ptr extract fail.
     *
     * @param var dynamic variable.
     */
    explicit CaseInsensitiveObjectWrapper(const Poco::Dynamic::Var& var);

    /**
     * Checks if key exists.
     *
     * @param key key.
     * @return bool.
     */
    bool Has(const std::string& key) const;

    /**
     * Gets value by key.
     *
     * @param key key.
     * @return Poco::Dynamic::Var.
     */
    Poco::Dynamic::Var Get(const std::string& key) const;

    /**
     * Gets value by key.
     *
     * @param key key.
     * @param defaultValue default value.
     * @return T.
     */
    template <typename T>
    T GetValue(const std::string& key, const T& defaultValue = T {}) const
    {
        if (Has(key)) {
            return Get(key).convert<T>();
        }

        return defaultValue;
    }

    /**
     * Gets optional value by key.
     *
     * @param key key.
     * @return std::optional<T>.
     */
    template <typename T>
    std::optional<T> GetOptionalValue(const std::string& key) const
    {
        if (Has(key)) {
            return Get(key).convert<T>();
        }

        return std::nullopt;
    }

    /**
     * Gets array by key.
     *
     * @param key key.
     * @return Poco::JSON::Array::Ptr.
     */
    Poco::JSON::Array::Ptr GetArray(const std::string& key) const;

    /**
     * Converts to Poco::JSON::Object::Ptr.
     *
     * @return Poco::JSON::Object::Ptr.
     */
    operator Poco::JSON::Object::Ptr() const;

    /**
     * Gets object by key.
     *
     * @param key key.
     * @return CaseInsensitiveObjectWrapper.
     */
    CaseInsensitiveObjectWrapper GetObject(const std::string& key) const;

private:
    std::string ToLowercase(const std::string& str) const;

    Poco::JSON::Object::Ptr                      mObject;
    std::unordered_map<std::string, std::string> mKeyMap;
};

/**
 * Gets value by key.
 *
 * @param object json object.
 * @param key key.
 * @param parserFunc function to parse array entities.
 * @return std::vector<T>.
 */
template <typename T, typename ParserFunc>
std::vector<T> GetArrayValue(const CaseInsensitiveObjectWrapper& object, const std::string& key, ParserFunc parserFunc)
{
    std::vector<T> result;

    if (!object.Has(key)) {
        return result;
    }

    Poco::JSON::Array::Ptr array = object.GetArray(key);

    std::transform(array->begin(), array->end(), std::back_inserter(result), parserFunc);

    return result;
}

/**
 * Gets value array by key.
 *
 * @param object json object.
 * @param key key.
 * @return std::vector<T>.
 */
template <typename T>
std::vector<T> GetArrayValue(const CaseInsensitiveObjectWrapper& object, const std::string& key)
{
    return GetArrayValue<T>(object, key, [](const Poco::Dynamic::Var& value) { return value.convert<T>(); });
}

/**
 * Stringifies json.
 *
 * @param json json object.
 * @return std::string.
 */
std::string Stringify(const Poco::Dynamic::Var& json);

} // namespace aos::common::utils

#endif
