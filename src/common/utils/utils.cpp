/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sstream>

#include <Poco/Base64Decoder.h>
#include <Poco/Base64Encoder.h>
#include <Poco/String.h>
#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include "utils.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

std::string NameUUID(const std::string& name)
{
    auto& generator = Poco::UUIDGenerator::defaultGenerator();

    return generator.createFromName(Poco::UUID::oid(), name).toString();
}

std::string Base64Decode(const std::string& encoded)
{
    std::istringstream  encodedStream(encoded);
    Poco::Base64Decoder decoder(encodedStream);

    return std::string(std::istreambuf_iterator<char>(decoder), std::istreambuf_iterator<char>());
}

std::string Base64Encode(std::string_view decoded)
{
    std::ostringstream  encodedStream;
    Poco::Base64Encoder encoder(encodedStream);

    encoder << decoded;
    encoder.close();

    return encodedStream.str();
}

} // namespace aos::common::utils
