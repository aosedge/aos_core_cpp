/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <iostream>
#include <numeric>
#include <sstream>

#include <Poco/String.h>

#include "aos/common/tools/string.hpp"

#include "common/utils/utils.hpp"

#include "partition.hpp"

using Poco::trimInPlace;

namespace aos::common::testtools {

namespace {

void CreateDisk(const std::string& path, uint64_t sizeMiB)
{
    utils::ExecCommand({"dd", "if=/dev/zero", "of=" + path, "bs=1M", "count=" + std::to_string(sizeMiB)});
    utils::ExecCommand({"parted", "-s", path, "mklabel", "gpt"});
}

void CreateParts(const std::string& path, const std::vector<PartDesc>& desc)
{
    // skip first MiB for GPT header
    uint64_t start = 1;

    for (auto const& p : desc) {
        uint64_t end = start + p.mSize;

        utils::ExecCommand(
            {"parted", "-s", path, "mkpart", "primary", std::to_string(start) + "MiB", std::to_string(end) + "MiB"});

        start = end;
    }
}

RetWithError<std::string> SetupLoop(const std::string& path)
{
    auto [out, err] = utils::ExecCommand({"losetup", "-f", "-P", "--show", path});
    if (!err.IsNone()) {
        return {out, err};
    }

    trimInPlace(out);

    return {out, ErrorEnum::eNone};
}

std::string ExtractPartUUID(const std::string& token)
{
    const std::string prefix = "PARTUUID=";
    std::string       s      = token;

    if (s.rfind(prefix, 0) == 0) {
        s = s.substr(prefix.size());
    }

    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }

    return s;
}

RetWithError<std::string> GetPartUUID(const std::string& device)
{
    auto [out, err] = utils::ExecCommand({"blkid", device});
    if (!err.IsNone()) {
        return {out, err};
    }

    trimInPlace(out);

    std::istringstream iss(out);
    std::string        token;

    while (iss >> token) {
        if (token.rfind("PARTUUID=", 0) == 0) {
            return ExtractPartUUID(token);
        }
    }

    return {out, ErrorEnum::eNotFound};
}

RetWithError<std::vector<PartInfo>> FormatDisk(const std::string& loopDev, const std::vector<PartDesc>& desc)
{
    std::vector<PartInfo> infos;

    infos.reserve(desc.size());

    for (size_t i = 0; i < desc.size(); ++i) {
        PartInfo info;
        info.mType   = desc[i].mType;
        info.mLabel  = desc[i].mLabel;
        info.mSize   = desc[i].mSize;
        info.mDevice = loopDev + "p" + std::to_string(i + 1);

        auto [partUUID, err] = GetPartUUID(info.mDevice);
        if (!err.IsNone()) {
            return {infos, err};
        }

        info.mPartUUID = partUUID;

        std::string labelFlag
            = (info.mType.find("fat") != std::string::npos || info.mType.find("dos") != std::string::npos) ? "-n"
                                                                                                           : "-L";

        utils::ExecCommand({"mkfs." + info.mType, info.mDevice, labelFlag, info.mLabel});
        infos.push_back(std::move(info));
    }

    return infos;
}

} // namespace

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

RetWithError<TestDisk> NewTestDisk(const std::string& path, const std::vector<PartDesc>& desc)
{
    // skip 1M for GPT table etc. and add 1M after device
    uint64_t totalSize = std::accumulate(
        desc.begin(), desc.end(), uint64_t {2}, [](uint64_t sum, const PartDesc& p) { return sum + p.mSize; });

    TestDisk disk(path);

    CreateDisk(path, totalSize);
    CreateParts(path, desc);
    auto [loopDev, err] = SetupLoop(path);
    if (!err.IsNone()) {
        return {disk, err};
    }

    disk.mDevice                 = loopDev;
    auto [partitions, errFormat] = FormatDisk(loopDev, desc);
    if (!errFormat.IsNone()) {
        return {disk, errFormat};
    }

    disk.mPartitions = partitions;

    return disk;
}

Error TestDisk::Close()
{
    if (!mDevice.empty()) {
        auto [_, err] = utils::ExecCommand({"losetup", "-d", mDevice});
        if (!err.IsNone()) {
            return err;
        }
    }

    if (std::filesystem::exists(mPath)) {
        auto [_, err] = utils::ExecCommand({"rm", "-rf", mPath});
        if (!err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::testtools
