/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>

#include "utils/json.hpp"
#include "visconfig.hpp"

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

VISConfig::VISConfig(
    const std::string& visServer, const std::string& caCertFile, const UtilsTime::Duration& webSocketTimeout)
    : mVISServer(visServer)
    , mCaCertFile(caCertFile)
    , mWebSocketTimeout(webSocketTimeout)
{
}

aos::Error VISConfig::Init(const Poco::Dynamic::Var& params)
{
    auto var = UtilsJson::FindByPath(params, {cVisServerTagName});

    if (var.isEmpty()) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eNotFound);
    }

    if (!var.isString()) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eInvalidArgument);
    }

    mVISServer = var.extract<std::string>();

    var = UtilsJson::FindByPath(params, {cCaCertFileTagName});
    if (var.isString()) {
        mCaCertFile = var.extract<std::string>();
    }

    var = UtilsJson::FindByPath(params, {cWebSocketTimeoutTagName});
    if (var.isString()) {
        aos::Error          err;
        UtilsTime::Duration duration;

        aos::Tie(duration, err) = UtilsTime::ParseDuration(var.extract<std::string>());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mWebSocketTimeout = std::move(duration);
    }

    return aos::ErrorEnum::eNone;
}

const std::string& VISConfig::GetVISServer() const
{
    return mVISServer;
}

const std::string& VISConfig::GetCaCertFile() const
{
    return mCaCertFile;
}

const UtilsTime::Duration& VISConfig::GetWebSocketTimeout() const
{
    return mWebSocketTimeout;
}

Poco::Dynamic::Var VISConfig::ToJSON() const
{
    Poco::JSON::Object object;

    object.set(cVisServerTagName, mVISServer);
    object.set(cCaCertFileTagName, mCaCertFile);
    object.set(cWebSocketTimeoutTagName, std::to_string(mWebSocketTimeout.count()).append("ns"));

    return object;
}

std::string VISConfig::ToString() const
{
    std::ostringstream jsonStream;

    Poco::JSON::Stringifier::stringify(ToJSON(), jsonStream);

    return jsonStream.str();
}
