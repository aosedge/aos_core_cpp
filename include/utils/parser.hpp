/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARSER_HPP_
#define PARSER_HPP_

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
 * Parses key-value pair from the specified line.
 *
 * @param line Line to parse.
 * @param trim Flag to trim key and value.
 * @param delimiter Delimiter to separate key and value.
 *
 * @return std::optional<KeyValue>.
 */
std::optional<KeyValue> ParseKeyValue(const std::string& line, bool trim = true, const std::string& delimiter = ":");

} // namespace aos::common::utils

#endif
