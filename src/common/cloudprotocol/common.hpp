/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_COMMON_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_COMMON_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/common.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Message type.
 */
class MessageTypeType {
public:
    enum class Enum {
        eAck,
        eAlerts,
        eBlobUrls,
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
        eRequestBlobUrls,
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
            "blobUrls",
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
            "requestBlobUrls",
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
 * AOS identity structure.
 */
struct AosIdentity {
    std::optional<std::string>    mID;
    std::optional<std::string>    mCodename;
    std::optional<UpdateItemType> mType;
    std::optional<std::string>    mTitle;
};

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
 * @param identity AosIdentity object to convert.
 * @return Poco::JSON::Object::Ptr.
 */
Poco::JSON::Object::Ptr CreateAosIdentity(const AosIdentity& identity);

/**
 * Parses AosIdentity object from JSON.
 *
 * @param json json object representation.
 * @param[out] identity object to fill.
 * @return Error.
 */
Error ParseAosIdentity(const common::utils::CaseInsensitiveObjectWrapper& json, AosIdentity& identity);

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

/**
 * Parses labels from JSON array.
 *
 * @param object
 * @param outLabels
 * @return Error
 */
Error LabelsFromJSON(
    const common::utils::CaseInsensitiveObjectWrapper& object, Array<StaticString<cLabelNameLen>>& outLabels);

} // namespace aos::common::cloudprotocol

#endif
