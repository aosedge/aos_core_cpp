/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <unordered_map>

#include <Poco/DigestEngine.h>
#include <Poco/DigestStream.h>
#include <Poco/SHA2Engine.h>
#include <Poco/StreamCopier.h>
#include <Poco/StringTokenizer.h>

#include <common/utils/utils.hpp>

#include "exception.hpp"
#include "image.hpp"

namespace fs = std::filesystem;

namespace {

const std::unordered_map<std::string, std::regex> cAnchoredEncodedRegexps
    = {{"sha256", std::regex(R"(^[a-f0-9]{64}$)")}, {"sha384", std::regex(R"(^[a-f0-9]{96}$)")},
        {"sha512", std::regex(R"(^[a-f0-9]{128}$)")}};

} // namespace

namespace aos::common::utils {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static void ValidateEncoded(const std::string& algorithm, const std::string& encoded)
{
    auto it = cAnchoredEncodedRegexps.find(algorithm);
    if (it == cAnchoredEncodedRegexps.end()) {
        throw std::runtime_error("Unsupported algorithm");
    }

    const std::regex& r = it->second;

    if (encoded.length() != 2 * (algorithm == "sha256" ? 32 : algorithm == "sha384" ? 48 : 64)) {
        throw std::runtime_error("Invalid encoded length");
    }

    if (!std::regex_match(encoded, r)) {
        throw std::runtime_error("Invalid encoded");
    }
}

static std::vector<std::string> CollectFiles(const std::string& dir)
{
    std::vector<std::string> files;
    std::string              cleanDir = fs::canonical(dir).string();

    for (const auto& entry : fs::recursive_directory_iterator(cleanDir)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().generic_string());
        }
    }

    return files;
}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

std::pair<std::string, std::string> ParseDigest(const Digest& digest)
{
    auto pos = digest.find(':');
    if (pos == std::string::npos) {
        return {digest, ""};
    }

    return {digest.substr(0, pos), digest.substr(pos + 1)};
}

Error UnpackTarImage(const std::string& archivePath, const std::string& destination)
{
    if (!fs::exists(archivePath)) {
        return Error(ErrorEnum::eNotFound, "archive does not exist");
    }

    auto [_, err] = ExecCommand({"tar", "xf", archivePath, "-C", destination});
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

RetWithError<uint64_t> GetUnpackedArchiveSize(const std::string& archivePath, bool isTarGz)
{
    constexpr auto cFilePermissionStrLen     = 10;
    constexpr auto cFilePermissionTokenIndex = 0;
    constexpr auto cFileSizeTokenIndex       = 2;

    if (!fs::exists(archivePath)) {
        return {0, ErrorEnum::eNotFound};
    }

    auto [outStr, err] = ExecCommand({"tar", isTarGz ? "-tzvf" : "-tvf", archivePath});
    if (!err.IsNone()) {
        return {0, AOS_ERROR_WRAP(err)};
    }

    try {
        std::istringstream istr(outStr);
        std::string        line;
        uint64_t           size = 0;

        while (std::getline(istr, line)) {
            Poco::StringTokenizer tokenizer(
                line, " ", Poco::StringTokenizer::TOK_IGNORE_EMPTY | Poco::StringTokenizer::TOK_TRIM);

            if (tokenizer.count() <= cFileSizeTokenIndex
                || tokenizer[cFilePermissionTokenIndex].length() != cFilePermissionStrLen) {
                continue;
            }

            size += std::stoull(tokenizer[cFileSizeTokenIndex]);
        }

        return size;

    } catch (const std::exception& e) {
        return {0, AOS_ERROR_WRAP(ToAosError(e))};
    }
}

Error ValidateDigest(const Digest& digest)
{
    auto [algorithm, hex] = ParseDigest(digest);

    std::transform(algorithm.begin(), algorithm.end(), algorithm.begin(), ::tolower);

    if (cAnchoredEncodedRegexps.find(algorithm) == cAnchoredEncodedRegexps.end()) {
        return Error(ErrorEnum::eInvalidArgument, "unsupported algorithm");
    }

    try {
        ValidateEncoded(algorithm, hex);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(ToAosError(e, ErrorEnum::eInvalidArgument));
    }

    return ErrorEnum::eNone;
}

RetWithError<Digest> CalculateDirDigest(const std::string& dir)
{
    std::vector<std::string> files = CollectFiles(dir);

    std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return a < b; });

    Poco::SHA2Engine h;

    for (const auto& file : files) {
        if (file.find('\n') != std::string::npos) {
            return {"", Error(ErrorEnum::eInvalidArgument, "file names with new lines are not supported")};
        }

        std::ifstream fileStream(file, std::ios::binary);
        if (!fileStream.is_open()) {
            return {"", Error(ErrorEnum::eFailed, "failed to open file")};
        }

        Poco::SHA2Engine         hf;
        Poco::DigestOutputStream dos(hf);
        Poco::StreamCopier::copyStream(fileStream, dos);

        std::string hash = Poco::DigestEngine::digestToHex(hf.digest()) + "  " + fs::relative(file, dir).string();

        h.update(hash + "\n");
    }

    return "sha256:" + Poco::DigestEngine::digestToHex(h.digest());
}

} // namespace aos::common::utils
