/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_UTILS_HPP_
#define AOS_COMMON_UTILS_UTILS_HPP_

#include <string>
#include <vector>

#include <core/common/tools/error.hpp>
#include <core/common/types/common.hpp>

/***********************************************************************************************************************
 * std namespace
 **********************************************************************************************************************/

namespace std {

/**
 * Hash instance identifier.
 */
template <>
struct hash<aos::InstanceIdent> {
    size_t operator()(aos::InstanceIdent const& id) const noexcept
    {
        std::string_view s1 {id.mItemID.CStr()};
        std::string_view s2 {id.mSubjectID.CStr()};

        size_t h1 = std::hash<std::string_view> {}(s1);
        size_t h2 = std::hash<std::string_view> {}(s2);
        size_t h3 = std::hash<uint64_t> {}(id.mInstance);

        size_t seed = h1;

        seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);

        return seed;
    }
};

} // namespace std

namespace aos::common::utils {

/**
 * Execute command and return its output.
 *
 * @param args command arguments (first argument is program name).
 * @return RetWithError<std::string>.
 */
RetWithError<std::string> ExecCommand(const std::vector<std::string>& args);

/**
 * Generates name-based UUID.
 *
 * @param name name.
 * @return std::string.
 */
std::string NameUUID(const std::string& name);

/**
 * Decodes base64 encoded string.
 *
 * @param encoded base64 encoded string.
 * @return std::string.
 */
std::string Base64Decode(const std::string& encoded);

/**
 * Encodes string to base64.
 *
 * @param decoded string to encode.
 * @return std::string.
 */
std::string Base64Encode(const std::string& decoded);

} // namespace aos::common::utils

#endif
