/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits>

#include <Poco/StringTokenizer.h>

#include "parser.hpp"

namespace aos::common::utils {

std::optional<KeyValue> ParseKeyValue(const std::string& line, bool trim, const std::string& delimiter)
{
    const auto flags = Poco::StringTokenizer::TOK_IGNORE_EMPTY | (trim ? Poco::StringTokenizer::TOK_TRIM : 0);

    Poco::StringTokenizer tokenizer(line, delimiter, flags);

    if (tokenizer.count() != 2) {
        return std::nullopt;
    }

    return KeyValue {tokenizer[0], tokenizer[1]};
}

std::optional<PortRange> ParsePortRange(const std::string& value)
{
    constexpr size_t cMaxPortDigits = 5;

    uint32_t bound[2] {};
    size_t   digits[2] {};
    size_t   index = 0;

    for (const auto c : value) {
        if (c == ':') {
            if (++index > 1) {
                return std::nullopt;
            }

            continue;
        }

        if (c < '0' || c > '9' || ++digits[index] > cMaxPortDigits) {
            return std::nullopt;
        }

        bound[index] = bound[index] * 10 + static_cast<uint32_t>(c - '0');
    }

    if (digits[0] == 0 || (index == 1 && digits[1] == 0)) {
        return std::nullopt;
    }

    const auto first = bound[0];
    const auto last  = index == 0 ? bound[0] : bound[1];

    if (first == 0 || first > last || last > std::numeric_limits<uint16_t>::max()) {
        return std::nullopt;
    }

    return PortRange {static_cast<uint16_t>(first), static_cast<uint16_t>(last)};
}

} // namespace aos::common::utils
