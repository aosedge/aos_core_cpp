/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <core/common/cloudprotocol/protocol.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "alerts.hpp"
#include "common.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr AlertItemToJSON(const aos::cloudprotocol::AlertItem& item)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto time = item.mTimestamp.ToUTCString();
    AOS_ERROR_CHECK_AND_THROW(time.mError, "failed to convert timestamp to UTC string");

    json->set("timestamp", time.mValue.CStr());
    json->set("tag", item.mTag.ToString().CStr());

    return json;
}

void AlertItemFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::AlertItem& item)
{
    Error err;

    Tie(item.mTimestamp, err) = Time::UTC(json.GetValue<std::string>("timestamp").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse timestamp from JSON");

    err = item.mTag.FromString(json.GetValue<std::string>("tag").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse tag from JSON");
}

Poco::JSON::Object::Ptr CoreAlertToJSON(const aos::cloudprotocol::CoreAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("nodeId", alert.mNodeID.CStr());
    json->set("coreComponent", alert.mCoreComponent.ToString().CStr());
    json->set("message", alert.mMessage.CStr());

    return json;
}

void CoreAlertFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::CoreAlert& alert)
{
    AlertItemFromJSON(json, alert);

    auto err = alert.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse nodeId from JSON");

    err = alert.mCoreComponent.FromString(json.GetValue<std::string>("coreComponent").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse coreComponent from JSON");

    err = alert.mMessage.Assign(json.GetValue<std::string>("message").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse message from JSON");
}

Poco::JSON::Object::Ptr DeviceAllocateAlertToJSON(const aos::cloudprotocol::DeviceAllocateAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    auto err = ToJSON(alert.mInstanceIdent, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instanceIdent to JSON");

    json->set("nodeId", alert.mNodeID.CStr());
    json->set("deviceId", alert.mDevice.CStr());
    json->set("message", alert.mMessage.CStr());

    return json;
}

void DeviceAllocateAlertFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DeviceAllocateAlert& alert)
{
    AlertItemFromJSON(json, alert);

    auto err = FromJSON(json, alert.mInstanceIdent);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse instanceIdent from JSON");

    err = alert.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse nodeId from JSON");

    err = alert.mDevice.Assign(json.GetValue<std::string>("deviceId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse deviceId from JSON");

    err = alert.mMessage.Assign(json.GetValue<std::string>("message").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse message from JSON");
}

Poco::JSON::Object::Ptr DownloadAlertToJSON(const aos::cloudprotocol::DownloadAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("targetType", alert.mTargetType.ToString().CStr());
    json->set("targetId", alert.mTargetID.CStr());
    json->set("version", alert.mVersion.CStr());
    json->set("message", alert.mMessage.CStr());
    json->set("url", alert.mURL.CStr());

    auto [uintVal, err] = alert.mDownloadedBytes.ToUint64();
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse downloadedBytes as unsigned integer");

    json->set("downloadedBytes", uintVal);

    Tie(uintVal, err) = alert.mTotalBytes.ToUint64();
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse totalBytes as unsigned integer");

    json->set("totalBytes", uintVal);

    return json;
}

void DownloadAlertFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DownloadAlert& alert)
{
    AlertItemFromJSON(json, alert);

    auto err = alert.mTargetType.FromString(json.GetValue<std::string>("targetType").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse targetType from JSON");

    err = alert.mTargetID.Assign(json.GetValue<std::string>("targetId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse targetId from JSON");

    err = alert.mVersion.Assign(json.GetValue<std::string>("version").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse version from JSON");

    err = alert.mMessage.Assign(json.GetValue<std::string>("message").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse message from JSON");

    err = alert.mURL.Assign(json.GetValue<std::string>("url").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse url from JSON");

    err = alert.mDownloadedBytes.Assign(json.GetValue<std::string>("downloadedBytes").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse downloadedBytes from JSON");

    err = alert.mTotalBytes.Assign(json.GetValue<std::string>("totalBytes").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse totalBytes from JSON");
}

Poco::JSON::Object::Ptr InstanceQuotaAlertToJSON(const aos::cloudprotocol::InstanceQuotaAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    auto err = ToJSON(alert.mInstanceIdent, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instanceIdent to JSON");

    json->set("parameter", alert.mParameter.CStr());
    json->set("value", alert.mValue);

    return json;
}

void InstanceQuotaAlertFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::InstanceQuotaAlert& alert)
{
    AlertItemFromJSON(json, alert);

    auto err = FromJSON(json, alert.mInstanceIdent);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse instanceIdent from JSON");

    err = alert.mParameter.Assign(json.GetValue<std::string>("parameter").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse parameter from JSON");

    alert.mValue = json.GetValue<uint64_t>("value", 0);
}

Poco::JSON::Object::Ptr ServiceInstanceAlertToJSON(const aos::cloudprotocol::ServiceInstanceAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    auto err = ToJSON(alert.mInstanceIdent, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to convert instanceIdent to JSON");

    json->set("version", alert.mServiceVersion.CStr());
    json->set("message", alert.mMessage.CStr());

    return json;
}

void ServiceInstanceAlertFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::ServiceInstanceAlert& alert)
{
    AlertItemFromJSON(json, alert);

    auto err = FromJSON(json, alert.mInstanceIdent);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse instanceIdent from JSON");

    err = alert.mServiceVersion.Assign(json.GetValue<std::string>("version").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse version from JSON");

    err = alert.mMessage.Assign(json.GetValue<std::string>("message").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse message from JSON");
}

Poco::JSON::Object::Ptr SystemAlertToJSON(const aos::cloudprotocol::SystemAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("nodeId", alert.mNodeID.CStr());
    json->set("message", alert.mMessage.CStr());

    return json;
}

void SystemAlertFromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::SystemAlert& alert)
{
    AlertItemFromJSON(json, alert);

    auto err = alert.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse nodeId from JSON");

    err = alert.mMessage.Assign(json.GetValue<std::string>("message").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse message from JSON");
}

Poco::JSON::Object::Ptr SystemQuotaAlertToJSON(const aos::cloudprotocol::SystemQuotaAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("nodeId", alert.mNodeID.CStr());
    json->set("parameter", alert.mParameter.CStr());
    json->set("value", alert.mValue);

    return json;
}

void SystemQuotaAlertFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::SystemQuotaAlert& alert)
{
    AlertItemFromJSON(json, alert);

    auto err = alert.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse nodeId from JSON");

    err = alert.mParameter.Assign(json.GetValue<std::string>("parameter").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse parameter from JSON");

    alert.mValue = json.GetValue<uint64_t>("value", 0);
}

Poco::JSON::Object::Ptr ResourceValidateAlertToJSON(const aos::cloudprotocol::ResourceValidateAlert& alert)
{
    auto json = AlertItemToJSON(alert);

    json->set("nodeId", alert.mNodeID.CStr());
    json->set("name", alert.mName.CStr());

    json->set("errors", utils::ToJsonArray(alert.mErrors, [](const Error& item) {
        auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(item, *json);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert error to JSON");

        return json;
    }));

    return json;
}

void ResourceValidateAlertFromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::ResourceValidateAlert& alert)
{
    AlertItemFromJSON(json, alert);

    auto err = alert.mNodeID.Assign(json.GetValue<std::string>("nodeId").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse nodeId from JSON");

    err = alert.mName.Assign(json.GetValue<std::string>("name").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse name from JSON");

    utils::ForEach(json, "errors", [&alert](const auto& item) {
        auto err = alert.mErrors.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "failed to emplace error into ResourceValidateAlert");

        err = FromJSON(utils::CaseInsensitiveObjectWrapper(item), alert.mErrors.Back());
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse error from JSON");
    });
}

class ToJSONVisitor : public StaticVisitor<Poco::JSON::Object::Ptr> {
public:
    Res Visit(const aos::cloudprotocol::CoreAlert& alert) const { return CoreAlertToJSON(alert); }
    Res Visit(const aos::cloudprotocol::DeviceAllocateAlert& alert) const { return DeviceAllocateAlertToJSON(alert); }
    Res Visit(const aos::cloudprotocol::DownloadAlert& alert) const { return DownloadAlertToJSON(alert); }
    Res Visit(const aos::cloudprotocol::InstanceQuotaAlert& alert) const { return InstanceQuotaAlertToJSON(alert); }
    Res Visit(const aos::cloudprotocol::ServiceInstanceAlert& alert) const { return ServiceInstanceAlertToJSON(alert); }
    Res Visit(const aos::cloudprotocol::SystemAlert& alert) const { return SystemAlertToJSON(alert); }
    Res Visit(const aos::cloudprotocol::SystemQuotaAlert& alert) const { return SystemQuotaAlertToJSON(alert); }
    Res Visit(const aos::cloudprotocol::ResourceValidateAlert& alert) const
    {
        return ResourceValidateAlertToJSON(alert);
    }

    template <typename T>
    Res Visit(const T&) const
    {
        AOS_ERROR_THROW(ErrorEnum::eNotSupported, "Unsupported alert type for JSON conversion");
    }
};

class FromJSONVisitor : public StaticVisitor<void> {
public:
    explicit FromJSONVisitor(const utils::CaseInsensitiveObjectWrapper& json)
        : mJson(json)
    {
    }

    void Visit(aos::cloudprotocol::CoreAlert& alert) const { CoreAlertFromJSON(mJson, alert); }
    void Visit(aos::cloudprotocol::DeviceAllocateAlert& alert) const { DeviceAllocateAlertFromJSON(mJson, alert); }
    void Visit(aos::cloudprotocol::DownloadAlert& alert) const { DownloadAlertFromJSON(mJson, alert); }
    void Visit(aos::cloudprotocol::InstanceQuotaAlert& alert) const { InstanceQuotaAlertFromJSON(mJson, alert); }
    void Visit(aos::cloudprotocol::ServiceInstanceAlert& alert) const { ServiceInstanceAlertFromJSON(mJson, alert); }
    void Visit(aos::cloudprotocol::SystemAlert& alert) const { SystemAlertFromJSON(mJson, alert); }
    void Visit(aos::cloudprotocol::SystemQuotaAlert& alert) const { SystemQuotaAlertFromJSON(mJson, alert); }
    void Visit(aos::cloudprotocol::ResourceValidateAlert& alert) const { ResourceValidateAlertFromJSON(mJson, alert); }

    template <typename T>
    void Visit(T&) const
    {
        AOS_ERROR_THROW(ErrorEnum::eNotSupported, "Unsupported alert type for JSON conversion");
    }

private:
    utils::CaseInsensitiveObjectWrapper mJson;
};

void SetVariant(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::AlertVariant& alert)
{
    aos::cloudprotocol::AlertTag tag;

    if (auto err = tag.FromString(json.GetValue<std::string>("tag").c_str()); !err.IsNone()) {
        AOS_ERROR_CHECK_AND_THROW(err, "Invalid tag field in JSON");
    }

    switch (tag.GetValue()) {
    case aos::cloudprotocol::AlertTagEnum::eCoreAlert: {
        alert.SetValue<aos::cloudprotocol::CoreAlert>();
        break;
    }
    case aos::cloudprotocol::AlertTagEnum::eDeviceAllocateAlert: {
        alert.SetValue<aos::cloudprotocol::DeviceAllocateAlert>();
        break;
    }
    case aos::cloudprotocol::AlertTagEnum::eDownloadProgressAlert: {
        alert.SetValue<aos::cloudprotocol::DownloadAlert>();
        break;
    }
    case aos::cloudprotocol::AlertTagEnum::eInstanceQuotaAlert: {
        alert.SetValue<aos::cloudprotocol::InstanceQuotaAlert>();
        break;
    }
    case aos::cloudprotocol::AlertTagEnum::eServiceInstanceAlert: {
        alert.SetValue<aos::cloudprotocol::ServiceInstanceAlert>();
        break;
    }
    case aos::cloudprotocol::AlertTagEnum::eSystemAlert: {
        alert.SetValue<aos::cloudprotocol::SystemAlert>();
        break;
    }
    case aos::cloudprotocol::AlertTagEnum::eSystemQuotaAlert: {
        alert.SetValue<aos::cloudprotocol::SystemQuotaAlert>();
        break;
    }
    case aos::cloudprotocol::AlertTagEnum::eResourceValidateAlert: {
        alert.SetValue<aos::cloudprotocol::ResourceValidateAlert>();
        break;
    }
    default:
        AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, tag.ToString().CStr());
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::Alerts& alerts)
{
    constexpr aos::cloudprotocol::MessageType cMessageType = aos::cloudprotocol::MessageTypeEnum::eAlerts;

    try {
        if (json.GetValue<std::string>("messageType") != cMessageType.ToString().CStr()) {
            return Error(ErrorEnum::eInvalidArgument, "Invalid messageType field in JSON");
        }

        utils::ForEach(json, "items", [&alerts](const auto& item) {
            auto err = alerts.mItems.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "failed to emplace alert into AlertVariantStaticArray");

            auto wrapper = utils::CaseInsensitiveObjectWrapper(item);

            SetVariant(wrapper, alerts.mItems.Back());

            alerts.mItems.Back().ApplyVisitor(FromJSONVisitor(wrapper));
        });
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::Alerts& alerts, Poco::JSON::Object& json)
{
    constexpr aos::cloudprotocol::MessageType cMessageType = aos::cloudprotocol::MessageTypeEnum::eAlerts;

    try {
        json.set("messageType", cMessageType.ToString().CStr());
        json.set("items",
            utils::ToJsonArray(alerts.mItems, [](const auto& item) { return item.ApplyVisitor(ToJSONVisitor()); }));
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
