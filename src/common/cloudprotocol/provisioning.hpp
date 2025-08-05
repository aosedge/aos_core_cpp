/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_PROVISIONING_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_PROVISIONING_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/cloudprotocol/provisioning.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to start provisioning request object.
 *
 * @param json json object representation.
 * @param[out] request start provisioning request object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::StartProvisioningRequest& request);

/**
 * Converts start provisioning request object to JSON object.
 *
 * @param request start provisioning request object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::StartProvisioningRequest& request, Poco::JSON::Object& json);

/**
 * Converts JSON object to start provisioning response object.
 *
 * @param json json object representation.
 * @param[out] response start provisioning response object to fill.
 * @return Error.
 */
Error FromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::StartProvisioningResponse& response);

/**
 * Converts start provisioning response object to JSON object.
 *
 * @param response start provisioning response object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::StartProvisioningResponse& response, Poco::JSON::Object& json);

/**
 * Converts JSON object to finish provisioning request object.
 *
 * @param json json object representation.
 * @param[out] request finish provisioning request object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::FinishProvisioningRequest& request);

/**
 * Converts finish provisioning request object to JSON object.
 *
 * @param request finish provisioning request object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::FinishProvisioningRequest& request, Poco::JSON::Object& json);

/**
 * Converts JSON object to finish provisioning response object.
 *
 * @param json json object representation.
 * @param[out] response finish provisioning response object to fill.
 * @return Error.
 */
Error FromJSON(
    const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::FinishProvisioningResponse& response);

/**
 * Converts finish provisioning response object to JSON object.
 * @param response finish provisioning response object to convert.
 * @param[out] json JSON object to fill
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::FinishProvisioningResponse& response, Poco::JSON::Object& json);

/**
 * Converts JSON object to deprovisioning request object.
 *
 * @param json json object representation.
 * @param[out] request deprovisioning request object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DeprovisioningRequest& request);

/**
 * Converts deprovisioning request object to JSON object.
 *
 * @param request deprovisioning request object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::DeprovisioningRequest& request, Poco::JSON::Object& json);

/**
 * Converts JSON object to deprovisioning response object.
 *
 * @param json json object representation.
 * @param[out] response deprovisioning response object to fill.
 * @return Error.
 */
Error FromJSON(const utils::CaseInsensitiveObjectWrapper& json, aos::cloudprotocol::DeprovisioningResponse& response);

/**
 * Converts deprovisioning response object to JSON object.
 *
 * @param response deprovisioning response object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const aos::cloudprotocol::DeprovisioningResponse& response, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
