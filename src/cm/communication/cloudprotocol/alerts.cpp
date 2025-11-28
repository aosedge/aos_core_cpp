/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "alerts.hpp"
#include "common.hpp"

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr AlertItemToJSON(const AlertItem& item)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto time = item.mTimestamp.ToUTCString();
    AOS_ERROR_CHECK_AND_THROW(time.mError, "failed to convert timestamp to UTC string");

    json->set("timestamp", time.mValue.CStr());
    json->set("tag", item.mTag.ToString().CStr());

    return json;
}

Poco::JSON::Object::Ptr CoreAlertToJSON(const CoreAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("node", CreateAosIdentity({alert.mNodeID}));
    json->set("coreComponent", alert.mCoreComponent.ToString().CStr());
    json->set("message", alert.mMessage.CStr());

    return json;
}

Poco::JSON::Object::Ptr ResourceAllocateAlertToJSON(const ResourceAllocateAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    auto err = ToJSON(static_cast<const InstanceIdent&>(alert), *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instanceIdent to JSON");

    json->set("node", CreateAosIdentity({alert.mNodeID}));
    json->set("deviceId", alert.mResource.CStr());
    json->set("message", alert.mMessage.CStr());

    return json;
}

Poco::JSON::Object::Ptr DownloadAlertToJSON(const DownloadAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("digest", alert.mDigest.CStr());
    json->set("url", alert.mURL.CStr());
    json->set("downloadedBytes", alert.mDownloadedBytes);
    json->set("totalBytes", alert.mTotalBytes);
    json->set("state", alert.mState.ToString().CStr());

    if (alert.mReason.HasValue()) {
        json->set("reason", alert.mReason->CStr());
    }

    if (!alert.mError.IsNone()) {
        auto jsonError = Poco::makeShared<Poco::JSON::Object>();

        auto err = ToJSON(alert.mError, *jsonError);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert errorInfo to JSON");

        json->set("errorInfo", jsonError);
    }

    return json;
}

Poco::JSON::Object::Ptr InstanceQuotaAlertToJSON(const InstanceQuotaAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    auto err = ToJSON(static_cast<const InstanceIdent&>(alert), *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instanceIdent to JSON");

    json->set("parameter", alert.mParameter.CStr());
    json->set("value", alert.mValue);

    return json;
}

Poco::JSON::Object::Ptr InstanceAlertToJSON(const InstanceAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    auto err = ToJSON(static_cast<const InstanceIdent&>(alert), *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instanceIdent to JSON");

    json->set("version", alert.mVersion.CStr());
    json->set("message", alert.mMessage.CStr());

    return json;
}

Poco::JSON::Object::Ptr SystemAlertToJSON(const SystemAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("node", CreateAosIdentity({alert.mNodeID}));
    json->set("message", alert.mMessage.CStr());

    return json;
}

Poco::JSON::Object::Ptr SystemQuotaAlertToJSON(const SystemQuotaAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("node", CreateAosIdentity({alert.mNodeID}));
    json->set("parameter", alert.mParameter.CStr());
    json->set("value", alert.mValue);

    return json;
}

class ToJSONVisitor : public StaticVisitor<Poco::JSON::Object::Ptr> {
public:
    Res Visit(const CoreAlert& alert) const { return CoreAlertToJSON(alert); }
    Res Visit(const ResourceAllocateAlert& alert) const { return ResourceAllocateAlertToJSON(alert); }
    Res Visit(const DownloadAlert& alert) const { return DownloadAlertToJSON(alert); }
    Res Visit(const InstanceQuotaAlert& alert) const { return InstanceQuotaAlertToJSON(alert); }
    Res Visit(const InstanceAlert& alert) const { return InstanceAlertToJSON(alert); }
    Res Visit(const SystemAlert& alert) const { return SystemAlertToJSON(alert); }
    Res Visit(const SystemQuotaAlert& alert) const { return SystemQuotaAlertToJSON(alert); }
};

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ToJSON(const Alerts& alerts, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eAlerts;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(alerts), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        json.set("items", common::utils::ToJsonArray(alerts.mItems, [](const auto& item) {
            return item.ApplyVisitor(ToJSONVisitor());
        }));
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::communication::cloudprotocol
