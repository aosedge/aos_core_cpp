/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VISMESSAGE_HPP_
#define VISMESSAGE_HPP_

#include <array>
#include <string>

#include <Poco/JSON/Object.h>

#include <aos/common/tools/array.hpp>
#include <aos/common/tools/enum.hpp>

/**
 * Supported Vehicle Information Service actions.
 */
class VISActionType {
public:
    enum class Enum {
        eGet,
        eSubscribe,
        eSubscriptionNotification,
        eUnsubscribeAll,
        eNumActions,
    };

    static const aos::Array<const char* const> GetStrings()
    {
        static const char* const sVISActionTypeStrings[] = {
            "get",
            "subscribe",
            "subscription",
            "unsubscribeAll",
        };

        return aos::Array<const char* const>(sVISActionTypeStrings, aos::ArraySize(sVISActionTypeStrings));
    };
};

using VISActionEnum = VISActionType::Enum;
using VISAction     = aos::EnumStringer<VISActionType>;
using JsonObject    = Poco::JSON::Object;
using JsonObjectPtr = JsonObject::Ptr;

/**
 * Vehicle Information Service message
 */
class VISMessage {
public:
    static constexpr const char* cActionTagName         = "action";
    static constexpr const char* cPathTagName           = "path";
    static constexpr const char* cRequestIdTagName      = "requestId";
    static constexpr const char* cSubscriptionIdTagName = "subscriptionId";
    static constexpr const char* cValueTagName          = "value";

    /**
     * Creates Vehicle Information Service message.
     *
     * @param action The type of action requested by the client or delivered by the server.
     */
    VISMessage(const VISAction action);

    /**
     * Creates Vehicle Information Service message.
     *
     * @param action The type of action requested by the client or delivered by the server.
     * @param requestId request id.
     * @param path path.
     */
    VISMessage(const VISAction action, const std::string& requestId, const std::string& path);

    /**
     * Creates Vehicle Information Service message.
     *
     * @param jsonStr JSON string that contains Vehicle Information Service message.
     */
    VISMessage(const std::string& jsonStr);

    /**
     * Checks if Vehicle Information Service message has specified type.
     *
     * @param actionType action type to check.
     * @return bool result
     */
    bool Is(const VISAction actionType) const;

    /**
     * Return const Vehicle Information Service message json object
     *
     * @return const JsonObjectPtr&
     */
    const JsonObject& GetJSON() const;

    /**
     * Converts Vehicle Information Service message to string.
     *
     * @return std::string.
     */
    std::string ToString() const;

    /**
     * Converts Vehicle Information Service message to byte array.
     *
     * @return std::vector<uint8_t>.
     */
    std::vector<uint8_t> ToByteArray() const;

    /**
     * Sets Vehicle Information Service message key-value.
     *
     * @param key VIS message key.
     * @param value VIS message value.
     * @return
     */
    template <class V>
    void SetKeyValue(const std::string& key, const V& value)
    {
        mJsonObject.set(key, value);
    }

    /**
     * Gets Vehicle Information Service message value by key.
     *
     * @param key VIS message key.
     * @return
     */
    template <class T>
    T GetValue(const std::string& key) const
    {
        return mJsonObject.getValue<T>(key);
    }

private:
    VISAction  mAction;
    JsonObject mJsonObject;
};

#endif
