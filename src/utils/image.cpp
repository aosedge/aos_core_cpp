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
#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/SHA2Engine.h>
#include <Poco/StreamCopier.h>

#include "utils/image.hpp"

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

std::pair<std::string, std::string> ParseDigest(const std::string& digest)
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
        return Error(ErrorEnum::eNotFound, "Archive does not exist");
    }

    Poco::Process::Args args;
    args.push_back("xf");
    args.push_back(archivePath);
    args.push_back("-C");
    args.push_back(destination);

    Poco::Pipe          outPipe;
    Poco::ProcessHandle ph = Poco::Process::launch("tar", args, nullptr, &outPipe, &outPipe);
    int                 rc = ph.wait();

    if (rc != 0) {
        std::string           output;
        Poco::PipeInputStream istr(outPipe);
        Poco::StreamCopier::copyToString(istr, output);

        return Error(ErrorEnum::eFailed, output.c_str());
    }

    return ErrorEnum::eNone;
}

Error ValidateDigest(const Digest& digest)
{
    auto [algorithm, hex] = ParseDigest(digest);

    std::transform(algorithm.begin(), algorithm.end(), algorithm.begin(), ::tolower);

    if (cAnchoredEncodedRegexps.find(algorithm) == cAnchoredEncodedRegexps.end()) {
        return Error(ErrorEnum::eInvalidArgument, "Unsupported algorithm");
    }

    try {
        ValidateEncoded(algorithm, hex);
    } catch (const std::runtime_error& e) {
        return Error(ErrorEnum::eInvalidArgument, e.what());
    }

    return ErrorEnum::eNone;
}

RetWithError<std::string> HashDir(const std::string& dir)
{
    std::vector<std::string> files = CollectFiles(dir);

    Poco::SHA2Engine h;
    for (const auto& file : files) {
        if (file.find('\n') != std::string::npos) {
            return {"", Error(ErrorEnum::eInvalidArgument, "File names with new lines are not supported")};
        }

        std::ifstream fileStream(file, std::ios::binary);
        if (!fileStream.is_open()) {
            return {"", Error(ErrorEnum::eFailed, "Failed to open file")};
        }

        Poco::SHA2Engine         hf;
        Poco::DigestOutputStream dos(hf);
        Poco::StreamCopier::copyStream(fileStream, dos);

        std::string hash = Poco::DigestEngine::digestToHex(hf.digest());
        h.update(hash + "\n");
    }

    return "sha256:" + Poco::DigestEngine::digestToHex(h.digest());
}

} // namespace aos::common::utils
