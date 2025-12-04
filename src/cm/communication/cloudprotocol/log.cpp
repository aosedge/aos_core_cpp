/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "common.hpp"
#include "log.hpp"

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void LogFilterFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, LogFilter& filter)
{
    if (json.Has("from")) {
        auto from = Time::UTC(json.GetValue<std::string>("from").c_str());
        AOS_ERROR_CHECK_AND_THROW(from.mError, "can't parse from time");

        filter.mFrom.EmplaceValue(from.mValue);
    }

    if (json.Has("till")) {
        auto till = Time::UTC(json.GetValue<std::string>("till").c_str());
        AOS_ERROR_CHECK_AND_THROW(till.mError, "can't parse till time");

        filter.mTill.EmplaceValue(till.mValue);
    }

    common::utils::ForEach(json, "nodeIds", [&filter](const auto& itemJson) {
        auto err = filter.mNodes.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to add nodeID to log filter");

        AosIdentity identity;

        err = ParseAosIdentity(common::utils::CaseInsensitiveObjectWrapper(itemJson), identity);
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node ID");

        if (!identity.mCodename.has_value()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound, "node codename is missing");
        }

        err = filter.mNodes.Back().Assign(identity.mCodename->c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse node ID");
    });

    auto err = FromJSON(json, static_cast<InstanceFilter&>(filter));
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse instance filter");
}

void LogUploadOptionsFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, LogUploadOptions& options)
{
    auto err = options.mType.FromString(json.GetValue<std::string>("type").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse log upload type");

    err = options.mURL.Assign(json.GetValue<std::string>("url").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse log upload URL");

    err = options.mBearerToken.Assign(json.GetValue<std::string>("bearerToken").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse bearer token");

    if (json.Has("bearerTokenTtl")) {
        auto bearerTokenTTL = Time::UTC(json.GetValue<std::string>("bearerTokenTtl").c_str());
        AOS_ERROR_CHECK_AND_THROW(bearerTokenTTL.mError, "can't parse bearer token TTL");

        options.mBearerTokenTTL.EmplaceValue(bearerTokenTTL.mValue);
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ToJSON(const PushLog& pushLog, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::ePushLog;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(pushLog), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        AosIdentity identity;
        identity.mCodename = pushLog.mNodeID.CStr();

        json.set("node", CreateAosIdentity(identity));
        json.set("part", pushLog.mPart);
        json.set("partsCount", pushLog.mPartsCount);
        json.set("content", pushLog.mContent.CStr());
        json.set("status", pushLog.mStatus.ToString().CStr());

        if (!pushLog.mError.IsNone()) {
            auto errorInfo = Poco::makeShared<Poco::JSON::Object>();

            auto err = ToJSON(pushLog.mError, *errorInfo);
            AOS_ERROR_CHECK_AND_THROW(err, "can't convert errorInfo to JSON");

            json.set("errorInfo", errorInfo);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, RequestLog& requestLog)
{
    try {
        auto err = FromJSON(json, static_cast<Protocol&>(requestLog));
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse protocol");

        err = requestLog.mLogType.FromString(json.GetValue<std::string>("logType").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse logType");

        if (!json.Has("filter")) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "filter is a required field"));
        }

        LogFilterFromJSON(json.GetObject("filter"), requestLog.mFilter);

        if (json.Has("uploadOptions")) {
            requestLog.mUploadOptions.EmplaceValue();

            LogUploadOptionsFromJSON(json.GetObject("uploadOptions"), requestLog.mUploadOptions.GetValue());
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
