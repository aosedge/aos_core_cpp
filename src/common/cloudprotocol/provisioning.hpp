/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_PROVISIONING_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_PROVISIONING_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/provisioning.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts JSON object to start provisioning request object.
 *
 * @param json json object representation.
 * @param[out] request start provisioning request object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, StartProvisioningRequest& request);

/**
 * Converts start provisioning response object to JSON object.
 *
 * @param response start provisioning response object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const StartProvisioningResponse& response, Poco::JSON::Object& json);

/**
 * Converts JSON object to finish provisioning request object.
 *
 * @param json json object representation.
 * @param[out] request finish provisioning request object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, FinishProvisioningRequest& request);

/**
 * Converts finish provisioning request object to JSON object.
 *
 * @param request finish provisioning request object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const FinishProvisioningRequest& request, Poco::JSON::Object& json);

/**
 * Converts finish provisioning response object to JSON object.
 * @param response finish provisioning response object to convert.
 * @param[out] json JSON object to fill
 * @return Error.
 */
Error ToJSON(const FinishProvisioningResponse& response, Poco::JSON::Object& json);

/**
 * Converts JSON object to deprovisioning request object.
 *
 * @param json json object representation.
 * @param[out] request deprovisioning request object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, DeprovisioningRequest& request);

/**
 * Converts deprovisioning response object to JSON object.
 *
 * @param response deprovisioning response object to convert.
 * @param[out] json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const DeprovisioningResponse& response, Poco::JSON::Object& json);

} // namespace aos::common::cloudprotocol

#endif
