/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "alerts.hpp"
#include "certificates.hpp"
#include "cloudmessage.hpp"
#include "desiredstatus.hpp"
#include "envvars.hpp"
#include "log.hpp"
#include "monitoring.hpp"
#include "provisioning.hpp"
#include "state.hpp"
#include "unitstatus.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Error EmplaceMessage(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::MessageVariant& message)
{
    aos::cloudprotocol::MessageType messageType;

    if (auto err = messageType.FromString(json.GetValue<std::string>("messageType").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "messageType parsing failed"));
    }

    switch (messageType.GetValue()) {
    case aos::cloudprotocol::MessageTypeEnum::eAlerts:
        message.SetValue<aos::cloudprotocol::Alerts>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eDeprovisioningRequest:
        message.SetValue<aos::cloudprotocol::DeprovisioningRequest>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eDeprovisioningResponse:
        message.SetValue<aos::cloudprotocol::DeprovisioningResponse>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eDesiredStatus:
        message.SetValue<aos::cloudprotocol::DesiredStatus>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eFinishProvisioningRequest:
        message.SetValue<aos::cloudprotocol::FinishProvisioningRequest>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eFinishProvisioningResponse:
        message.SetValue<aos::cloudprotocol::FinishProvisioningResponse>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eInstallUnitCertificatesConfirmation:
        message.SetValue<aos::cloudprotocol::InstallUnitCertsConfirmation>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eIssuedUnitCertificates:
        message.SetValue<aos::cloudprotocol::IssuedUnitCerts>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eIssueUnitCertificates:
        message.SetValue<aos::cloudprotocol::IssueUnitCerts>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eMonitoringData:
        message.SetValue<aos::cloudprotocol::Monitoring>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eNewState:
        message.SetValue<aos::cloudprotocol::NewState>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eOverrideEnvVars:
        message.SetValue<aos::cloudprotocol::OverrideEnvVarsRequest>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eOverrideEnvVarsStatus:
        message.SetValue<aos::cloudprotocol::OverrideEnvVarsStatuses>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::ePushLog:
        message.SetValue<aos::cloudprotocol::PushLog>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eRenewCertificatesNotification:
        message.SetValue<aos::cloudprotocol::RenewCertsNotification>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eRequestLog:
        message.SetValue<aos::cloudprotocol::RequestLog>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eStartProvisioningRequest:
        message.SetValue<aos::cloudprotocol::StartProvisioningRequest>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eStartProvisioningResponse:
        message.SetValue<aos::cloudprotocol::StartProvisioningResponse>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eStateAcceptance:
        message.SetValue<aos::cloudprotocol::StateAcceptance>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eStateRequest:
        message.SetValue<aos::cloudprotocol::StateRequest>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eUnitStatus:
        if (json.GetValue<bool>("isDeltaInfo", false)) {
            message.SetValue<aos::cloudprotocol::DeltaUnitStatus>();
            break;
        }

        message.SetValue<aos::cloudprotocol::UnitStatus>();
        break;

    case aos::cloudprotocol::MessageTypeEnum::eUpdateState:
        message.SetValue<aos::cloudprotocol::UpdateState>();
        break;

    default:
        LOG_WRN() << "Cloud message type is not supported" << Log::Field("messageType", messageType);

        return AOS_ERROR_WRAP(ErrorEnum::eNotSupported);
    }

    return ErrorEnum::eNone;
}

class FromJSONVisitor : public aos::StaticVisitor<Error> {
public:
    explicit FromJSONVisitor(const utils::CaseInsensitiveObjectWrapper& json)
        : mJson(json)
    {
    }

    template <typename T>
    Res Visit(T& val) const
    {
        return FromJSON(mJson, val);
    }

private:
    utils::CaseInsensitiveObjectWrapper mJson;
};

class ToJSONVisitor : public aos::StaticVisitor<Error> {
public:
    explicit ToJSONVisitor(Poco::JSON::Object& json)
        : mJson(json)
    {
    }

    template <typename T>
    Res Visit(const T& val) const
    {
        return ToJSON(val, mJson);
    }

private:
    Poco::JSON::Object& mJson;
};

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::MessageVariant& message)
{
    if (auto err = EmplaceMessage(json, message); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = message.ApplyVisitor(FromJSONVisitor(json)); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::MessageVariant& message, Poco::JSON::Object& json)
{
    return message.ApplyVisitor(ToJSONVisitor(json));
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::MessageHeader& header)
{
    if (!json.Has("version")) {
        return Error(ErrorEnum::eInvalidArgument, "version tag is missing");
    }

    if (!json.Has("systemID")) {
        return Error(ErrorEnum::eInvalidArgument, "systemID tag is missing");
    }

    header.mVersion = json.GetValue<size_t>("version");

    if (auto err = header.mSystemID.Assign(json.GetValue<std::string>("systemID", "").c_str()); !err.IsNone()) {
        return Error(err, "systemID parsing failed");
    }

    return ErrorEnum::eNone;
}

Error ToJSON(const aos::cloudprotocol::MessageHeader& header, Poco::JSON::Object& json)
{
    try {
        json.set("version", header.mVersion);
        json.set("systemID", header.mSystemID.CStr());
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return Error::Enum::eNone;
}

Error FromJSON(const std::string& json, aos::cloudprotocol::CloudMessage& message)
{
    try {
        auto [jsonObject, err] = utils::ParseJson(json);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse JSON");

        utils::CaseInsensitiveObjectWrapper objectWrapper(jsonObject);

        if (!objectWrapper.Has("header")) {
            return Error(ErrorEnum::eInvalidArgument, "header tag is required");
        }

        if (!objectWrapper.Has("data")) {
            return Error(ErrorEnum::eInvalidArgument, "data tag is required");
        }

        FromJSON(objectWrapper.GetObject("header"), message.mHeader);

        return FromJSON(objectWrapper.GetObject("data"), message.mData);
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return Error::Enum::eNone;
}

Error ToJSON(const aos::cloudprotocol::CloudMessage& message, Poco::JSON::Object& json)
{
    try {
        auto headerJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = ToJSON(message.mHeader, *headerJson);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert header to JSON");

        json.set("header", headerJson);

        auto dataJson = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        err = ToJSON(message.mData, *dataJson);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert data to JSON");

        json.set("data", dataJson);
    } catch (const std::exception& e) {
        return utils::ToAosError(e);
    }

    return Error::Enum::eNone;
}

} // namespace aos::common::cloudprotocol
