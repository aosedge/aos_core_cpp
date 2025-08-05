/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_CERTIFICATES_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_CERTIFICATES_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/cloudprotocol/certificates.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts issue certificate data object to JSON object.
 *
 * @param issueCertData issue certificate data object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::IssueCertData& issueCertData);

/**
 * Converts JSON object to issue certificate data object.
 *
 * @param json json object representation.
 * @param[out] issuedCertData issue certificate data object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::IssueCertData& issueCertData, Poco::JSON::Object& json);

/**
 * Converts issued certificate data object to JSON object.
 *
 * @param issuedCertData issued certificate data object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::IssuedCertData& issuedCertData);

/**
 * Converts JSON object to issued certificate data object.
 *
 * @param json json object representation.
 * @param[out] issuedCertData issued certificate data object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::IssuedCertData& issuedCertData, Poco::JSON::Object& json);

/**
 * Converts JSON object to renewCertsNotification object.
 *
 * @param json json object representation.
 * @param[out] renewCertsNotification renewCertsNotification object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json,
    aos::cloudprotocol::RenewCertsNotification&           renewCertsNotification);

/**
 * Converts renewCertsNotification object to JSON object.
 *
 * @param renewCertsNotification renewCertsNotification object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::RenewCertsNotification& renewCertsNotification, Poco::JSON::Object& json);

/**
 * Converts JSON object to request issued unit certs object.
 *
 * @param json json object representation.
 * @param[out] issuedUnitCerts issued unit certs object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::IssuedUnitCerts& issuedUnitCerts);

/**
 * Converts issued unit certs object to JSON object.
 *
 * @param issuedUnitCerts issued unit certs object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::IssuedUnitCerts& issuedUnitCerts, Poco::JSON::Object& json);

/**
 * Converts JSON object to request issue unit certs object.
 *
 * @param json json object representation.
 * @param[out] issueUnitCerts issue unit certs object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::IssueUnitCerts& issueUnitCerts);

/**
 * Converts issue unit certs object to JSON object.
 *
 * @param issueUnitCerts issue unit certs object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::IssueUnitCerts& issueUnitCerts, Poco::JSON::Object& json);

/**
 * Converts JSON object to request install unit certs confirmation object.
 *
 * @param json json object representation.
 * @param[out] confirmation install unit certs confirmation object to fill.
 * @return Error.
 */
Error FromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::InstallUnitCertsConfirmation& confirmation);

/**
 * Converts install unit certs confirmation object to JSON object.
 *
 * @param confirmation install unit certs confirmation object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::InstallUnitCertsConfirmation& confirmation, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
