/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/StringTokenizer.h>

#include "utils/parser.hpp"

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

} // namespace aos::common::utils
