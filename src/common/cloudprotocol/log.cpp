/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <core/common/cloudprotocol/cloudmessage.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "log.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr LogFilterToJSON(const aos::cloudprotocol::LogFilter& filter)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (filter.mFrom.HasValue()) {
        auto time = filter.mFrom.GetValue().ToUTCString();

        AOS_ERROR_CHECK_AND_THROW(time.mError, "failed to convert from time to UTC string");
        json->set("from", time.mValue.CStr());
    }

    if (filter.mTill.HasValue()) {
        auto time = filter.mTill.GetValue().ToUTCString();

        AOS_ERROR_CHECK_AND_THROW(time.mError, "failed to convert till time to UTC string");
        json->set("till", time.mValue.CStr());
    }

    if (!filter.mNodeIDs.IsEmpty()) {
        json->set("nodeIds", utils::ToJsonArray(filter.mNodeIDs, [](const auto& nodeID) { return nodeID.CStr(); }));
    }

    auto err = ToJSON(filter.mInstanceFilter, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instance filter to JSON");

    return json;
}

void LogFilterFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::LogFilter& filter)
{
    if (json.Has("from")) {
        auto from = Time::UTC(json.GetValue<std::string>("from").c_str());
        AOS_ERROR_CHECK_AND_THROW(from.mError, "failed to parse from time");

        filter.mFrom.EmplaceValue(from.mValue);
    }

    if (json.Has("till")) {
        auto till = Time::UTC(json.GetValue<std::string>("till").c_str());
        AOS_ERROR_CHECK_AND_THROW(till.mError, "failed to parse till time");

        filter.mTill.EmplaceValue(till.mValue);
    }

    for (const auto& nodeID : utils::GetArrayValue<std::string>(json, "nodeIds")) {
        auto err = filter.mNodeIDs.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add nodeID to log filter");

        err = filter.mNodeIDs.Back().Assign(nodeID.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add nodeID to log filter");
    }

    auto err = FromJSON(json, filter.mInstanceFilter);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse instance filter from JSON");
}

Poco::JSON::Object::Ptr LogUploadOptionsToJSON(const aos::cloudprotocol::LogUploadOptions& options)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("type", options.mType.ToString().CStr());
    json->set("url", options.mURL.CStr());
    json->set("bearerToken", options.mBearerToken.CStr());

    if (options.mBearerTokenTTL.HasValue()) {
        auto time = options.mBearerTokenTTL.GetValue().ToUTCString();

        AOS_ERROR_CHECK_AND_THROW(time.mError, "failed to convert bearerTokenTtl to UTC string");
        json->set("bearerTokenTtl", time.mValue.CStr());
    }

    return json;
}

void LogUploadOptionsFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::LogUploadOptions& options)
{
    auto err = options.mType.FromString(json.GetValue<std::string>("type").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse log upload type");

    err = options.mURL.Assign(json.GetValue<std::string>("url").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse log upload URL");

    err = options.mBearerToken.Assign(json.GetValue<std::string>("bearerToken").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse bearer token");

    if (json.Has("bearerTokenTtl")) {
        auto bearerTokenTTL = Time::UTC(json.GetValue<std::string>("bearerTokenTtl").c_str());
        AOS_ERROR_CHECK_AND_THROW(bearerTokenTTL.mError, "failed to parse bearer token TTL");

        options.mBearerTokenTTL.EmplaceValue(bearerTokenTTL.mValue);
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::PushLog& pushLog)
{
    try {
        auto err = pushLog.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse nodeId");

        err = pushLog.mLogID.Assign(json.GetValue<std::string>("logId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse logId");

        pushLog.mPart       = json.GetValue<uint64_t>("part", 0);
        pushLog.mPartsCount = json.GetValue<uint64_t>("partsCount", 0);

        err = pushLog.mContent.Assign(json.GetValue<std::string>("content").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse content");

        err = pushLog.mStatus.FromString(json.GetValue<std::string>("status").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse status");

        if (json.Has("errorInfo")) {
            err = FromJSON(json.GetObject("errorInfo"), pushLog.mErrorInfo);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse errorInfo");
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::PushLog& pushLog, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType = aos::cloudprotocol::MessageTypeEnum::ePushLog;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("logId", pushLog.mLogID.CStr());
        json.set("nodeId", pushLog.mNodeID.CStr());
        json.set("part", pushLog.mPart);
        json.set("partsCount", pushLog.mPartsCount);
        json.set("content", pushLog.mContent.CStr());
        json.set("status", pushLog.mStatus.ToString().CStr());

        if (!pushLog.mErrorInfo.IsNone()) {
            auto errorInfo = Poco::makeShared<Poco::JSON::Object>();

            auto err = ToJSON(pushLog.mErrorInfo, *errorInfo);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error info to JSON");

            json.set("errorInfo", errorInfo);
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::RequestLog& requestLog)
{
    try {
        auto err = requestLog.mLogID.Assign(json.GetValue<std::string>("logId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse logId");

        err = requestLog.mLogType.FromString(json.GetValue<std::string>("logType").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse logType");

        if (!json.Has("filter")) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "filter is a required field"));
        }

        LogFilterFromJSON(json.GetObject("filter"), requestLog.mFilter);

        if (json.Has("uploadOptions")) {
            requestLog.mUploadOptions.EmplaceValue();

            LogUploadOptionsFromJSON(json.GetObject("uploadOptions"), requestLog.mUploadOptions.GetValue());
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::RequestLog& requestLog, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType = aos::cloudprotocol::MessageTypeEnum::eRequestLog;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("logId", requestLog.mLogID.CStr());
        json.set("logType", requestLog.mLogType.ToString().CStr());
        json.set("filter", LogFilterToJSON(requestLog.mFilter));

        if (requestLog.mUploadOptions.HasValue()) {
            json.set("uploadOptions", LogUploadOptionsToJSON(requestLog.mUploadOptions.GetValue()));
        }
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
