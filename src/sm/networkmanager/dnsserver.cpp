/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <csignal>
#include <fstream>

#include <core/common/tools/logger.hpp>

#include "dnsserver.hpp"

namespace aos::sm::networkmanager {

namespace {

constexpr auto cHostsFileName = "addnhosts";

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error DNSServer::Init(const std::string& networkID, const std::string& storageDir,
    aos::common::process::ProcessSpawnerItf& spawner, Poco::Process::PID pid)
{
    mNetworkID  = networkID;
    mStorageDir = storageDir;
    mSpawner    = &spawner;
    mPID        = pid;

    if (auto err = WriteHostsFile(); !err.IsNone()) {
        return err;
    }

    return Reload();
}

Error DNSServer::AddHost(const String& instanceID, const DNSAliasesParams& params)
{
    LOG_DBG() << "Add DNS host" << Log::Field("instanceID", instanceID) << Log::Field("networkID", mNetworkID.c_str())
              << Log::Field("ip", params.mIP);

    HostRecord record;

    record.mIP = params.mIP.CStr();

    for (const auto& alias : params.mAliases) {
        std::string bare = alias.CStr();

        record.mNames.push_back(bare);
        record.mNames.push_back(bare + "." + mNetworkID);
    }

    mHosts[instanceID.CStr()] = std::move(record);

    if (auto err = WriteHostsFile(); !err.IsNone()) {
        return err;
    }

    return Reload();
}

Error DNSServer::RemoveHost(const String& instanceID)
{
    LOG_DBG() << "Remove DNS host" << Log::Field("instanceID", instanceID)
              << Log::Field("networkID", mNetworkID.c_str());

    if (mHosts.erase(instanceID.CStr()) == 0) {
        return ErrorEnum::eNone;
    }

    if (auto err = WriteHostsFile(); !err.IsNone()) {
        return err;
    }

    return Reload();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error DNSServer::WriteHostsFile() const
{
    const auto path = mStorageDir + "/" + cHostsFileName;

    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return Error(ErrorEnum::eRuntime, "failed to open addnhosts");
    }

    for (const auto& [_, record] : mHosts) {
        if (record.mNames.empty()) {
            continue;
        }

        std::string line = record.mIP;

        for (const auto& name : record.mNames) {
            line += "\t" + name;
        }

        line += "\n";

        file << line;
        if (file.fail()) {
            return Error(ErrorEnum::eRuntime, "failed to write addnhosts");
        }
    }

    return ErrorEnum::eNone;
}

Error DNSServer::Reload() const
{
    if (auto err = mSpawner->Signal(mPID, SIGHUP); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::networkmanager
