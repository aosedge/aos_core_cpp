/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UTILS_JSON_HPP_
#define UTILS_JSON_HPP_

#include <string>
#include <vector>

#include <Poco/Dynamic/Var.h>

#include <aos/common/tools/error.hpp>

namespace UtilsJson {
/**
 * Parses json string.
 *
 * @param json json string.
 * @return aos::RetWithError<Poco::Dynamic::Var> .
 */
aos::RetWithError<Poco::Dynamic::Var> ParseJson(const std::string& json) noexcept;

/**
 * Parses input stream.
 *
 * @param in input stream.
 * @return aos::RetWithError<Poco::Dynamic::Var> .
 */
aos::RetWithError<Poco::Dynamic::Var> ParseJson(std::istream& in) noexcept;

/**
 * Finds value of the json by path
 *
 * @param object json object.
 * @param path json path.
 * @return Poco::Dynamic::Var.
 */
Poco::Dynamic::Var FindByPath(const Poco::Dynamic::Var object, const std::vector<std::string>& path);

} // namespace UtilsJson

#endif
