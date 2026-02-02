/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_CLOUDPROTOCOL_CERTIFICATES_HPP_
#define AOS_CM_CLOUDPROTOCOL_CERTIFICATES_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/certificates.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to renewCertsNotification object.
 *
 * @param json json object representation.
 * @param[out] renewCertsNotification renewCertsNotification object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, RenewCertsNotification& renewCertsNotification);

/**
 * Converts issued unit certs object to JSON object.
 *
 * @param issuedUnitCerts issued unit certs object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, IssuedUnitCerts& issuedUnitCerts);

/**
 * Converts JSON object to issue unit certs object.
 *
 * @param json json object representation.
 * @param[out] issueUnitCerts issue unit certs object to fill.
 * @return Error.
 */
Error ToJSON(const IssueUnitCerts& issueUnitCerts, Poco::JSON::Object& json);

/**
 * Converts install unit certs confirmation object to JSON object.
 *
 * @param confirmation install unit certs confirmation object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const InstallUnitCertsConfirmation& confirmation, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
