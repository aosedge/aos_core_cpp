/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CLOUDPROTOCOL_BLOBS_HPP_
#define AOS_COMMON_CLOUDPROTOCOL_BLOBS_HPP_

#include <Poco/JSON/Object.h>

#include <core/common/types/blobs.hpp>

#include <common/utils/json.hpp>

namespace aos::common::cloudprotocol {

/**
 * Converts blob URLs request to JSON object.
 *
 * @param blobURLsRequest request to convert.
 * @param json JSON object to fill.
 * @return Error.
 */
Error ToJSON(const BlobURLsRequest& blobURLsRequest, Poco::JSON::Object& json);

/**
 * Converts JSON object to blob URLs info object.
 *
 * @param json json object representation.
 * @param[out] blobURLsInfo blob URLs info object to fill.
 * @return Error.
 */
Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, BlobURLsInfo& blobURLsInfo);

} // namespace aos::common::cloudprotocol

#endif
