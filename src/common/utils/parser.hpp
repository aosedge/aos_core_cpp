/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_PARSER_HPP_
#define AOS_COMMON_UTILS_PARSER_HPP_

#include <cstdint>
#include <optional>
#include <string>

namespace aos::common::utils {

/**
 * Key-value pair.
 */
struct KeyValue {
    std::string mKey;
    std::string mValue;
};

/**
 * Inclusive port range. A single port is represented as mFirst == mLast.
 */
struct PortRange {
    uint16_t mFirst {};
    uint16_t mLast {};

    /**
     * Compares port ranges.
     *
     * @param rhs Other port range.
     * @return bool.
     */
    bool operator==(const PortRange& rhs) const { return mFirst == rhs.mFirst && mLast == rhs.mLast; }

    /**
     * Compares port ranges.
     *
     * @param rhs Other port range.
     * @return bool.
     */
    bool operator!=(const PortRange& rhs) const { return !(*this == rhs); }
};

/**
 * Parses key-value pair from the specified line.
 *
 * @param line Line to parse.
 * @param trim Flag to trim key and value.
 * @param delimiter Delimiter to separate key and value.
 *
 * @return std::optional<KeyValue>.
 */
std::optional<KeyValue> ParseKeyValue(const std::string& line, bool trim = true, const std::string& delimiter = ":");

/**
 * Parses a single port ("1515") or an inclusive port range ("8089:8090").
 *
 * Decimal digits only: no whitespace, sign or any other separator is accepted.
 * Both bounds must satisfy 1 <= first <= last <= 65535.
 *
 * @param value Value to parse.
 * @return std::optional<PortRange>, empty when the value is not a valid port or range.
 */
std::optional<PortRange> ParsePortRange(const std::string& value);

} // namespace aos::common::utils

#endif
