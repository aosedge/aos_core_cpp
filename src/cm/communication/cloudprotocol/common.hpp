/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_CLOUDPROTOCOL_COMMON_HPP_
#define AOS_CM_COMMUNICATION_CLOUDPROTOCOL_COMMON_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/common.hpp>

#include <common/utils/json.hpp>

namespace aos::cm::communication::cloudprotocol {

/**
 * Message type.
 */
class MessageTypeType {
public:
    enum class Enum {
        eAck,
        eAlerts,
        eDeprovisioningRequest,
        eDeprovisioningResponse,
        eDesiredStatus,
        eFinishProvisioningRequest,
        eFinishProvisioningResponse,
        eInstallUnitCertificatesConfirmation,
        eIssuedUnitCertificates,
        eIssueUnitCertificates,
        eMonitoringData,
        eNack,
        eNewState,
        eOverrideEnvVars,
        eOverrideEnvVarsStatus,
        ePushLog,
        eRenewCertificatesNotification,
        eRequestLog,
        eStartProvisioningRequest,
        eStartProvisioningResponse,
        eStateAcceptance,
        eStateRequest,
        eUnitStatus,
        eUpdateState,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sStrings[] = {
            "ack",
            "alerts",
            "deprovisioningRequest",
            "deprovisioningResponse",
            "desiredStatus",
            "finishProvisioningRequest",
            "finishProvisioningResponse",
            "installUnitCertificatesConfirmation",
            "issuedUnitCertificates",
            "issueUnitCertificates",
            "monitoringData",
            "nack",
            "newState",
            "overrideEnvVars",
            "overrideEnvVarsStatus",
            "pushLog",
            "renewCertificatesNotification",
            "requestLog",
            "startProvisioningRequest",
            "startProvisioningResponse",
            "stateAcceptance",
            "stateRequest",
            "unitStatus",
            "updateState",
        };

        return Array<const char* const>(sStrings, ArraySize(sStrings));
    };
};

using MessageTypeEnum = MessageTypeType::Enum;
using MessageType     = EnumStringer<MessageTypeType>;

/**
 * Converts Error object to JSON object.
 *
 * @param error Error object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const Error& error, Poco::JSON::Object& json);

/**
 * Creates AosIdentity JSON object.
 *
 * @param id id to convert.
 * @param type type to convert.
 * @return Poco::JSON::Object::Ptr.
 */
Poco::JSON::Object::Ptr CreateAosIdentity(const Optional<String>& id, const Optional<UpdateItemType>& type = {});

/**
 * Parses id from AosIdentity JSON object.
 *
 * @param json json object representation.
 * @param[out] id id to fill.
 * @return Error.
 */
Error ParseAosIdentityID(const common::utils::CaseInsensitiveObjectWrapper& json, String& id);

/**
 * Converts InstanceIdent object to JSON.
 *
 * @param instanceIdent InstanceIdent object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const InstanceIdent& instanceIdent, Poco::JSON::Object& json);

/**
 * Converts JSON object to InstanceIdent.
 *
 * @param json JSON object to convert.
 * @param[out] instanceIdent InstanceIdent object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, InstanceIdent& instanceIdent);

/**
 * Converts JSON object to InstanceFilter.
 *
 * @param json JSON object to convert.
 * @param[out] instanceFilter InstanceFilter object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, InstanceFilter& instanceFilter);

/**
 * Converts protocol to JSON.
 *
 * @param protocol protocol to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const Protocol& protocol, Poco::JSON::Object& json);

/**
 * Converts JSON object to protocol.
 *
 * @param json JSON object to convert.
 * @param[out] protocol protocol to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, Protocol& protocol);

} // namespace aos::cm::communication::cloudprotocol

#endif
