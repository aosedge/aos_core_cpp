/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

#include <Poco/Exception.h>
#include <Poco/NumberParser.h>
#include <Poco/String.h>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>

#include "dnsname.hpp"

namespace aos::sm::networkmanager {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error DNSName::Init(const std::string& dnsStoragePath, aos::common::process::ProcessSpawnerItf& spawner)
{
    LOG_DBG() << "Init DNS name" << Log::Field("storagePath", dnsStoragePath.c_str());

    mDNSStoragePath = dnsStoragePath;
    mSpawner        = &spawner;

    return ErrorEnum::eNone;
}

Error DNSName::RemoveOrphans(const Array<StaticString<cIDLen>>& knownNetworkIDs)
{
    LOG_DBG() << "Remove orphan DNS servers";

    if (!std::filesystem::exists(mDNSStoragePath)) {
        return ErrorEnum::eNone;
    }

    std::set<std::string> known;

    for (const auto& id : knownNetworkIDs) {
        known.emplace(id.CStr());
    }

    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(mDNSStoragePath, ec)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto nid = entry.path().filename().string();

        if (known.count(nid) != 0) {
            continue;
        }

        LOG_WRN() << "Reap orphan DNS server" << Log::Field("networkID", nid.c_str());

        const auto storageDir = entry.path().string();

        if (auto [pid, err] = ReadPidFile(storageDir); err.IsNone()) {
            if (auto kerr = mSpawner->Kill(pid); !kerr.IsNone()) {
                LOG_ERR() << "Failed to kill orphan dnsmasq" << Log::Field("networkID", nid.c_str())
                          << Log::Field(kerr);
            }
        }

        WipeStorageDir(storageDir);
    }

    if (ec) {
        LOG_ERR() << "Failed to iterate DNS storage path" << Log::Field("err", ec.message().c_str());
    }

    return ErrorEnum::eNone;
}

RetWithError<DNSServerItf*> DNSName::CreateServer(const String& networkID, const DNSServerParams& params)
{
    const std::string nidStr = networkID.CStr();

    LOG_DBG() << "Create DNS server" << Log::Field("networkID", networkID) << Log::Field("bridgeIP", params.mBridgeIP)
              << Log::Field("bridgeIfName", params.mBridgeIfName);

    if (auto it = mServers.find(nidStr); it != mServers.end()) {
        return {it->second.mServer.get(), ErrorEnum::eNone};
    }

    const auto storageDir = StorageDirFor(nidStr);

    if (auto err = EnsureStorageDir(storageDir); !err.IsNone()) {
        return {nullptr, err};
    }

    Poco::Process::PID pid {};

    auto [existingPID, readErr] = ReadPidFile(storageDir);

    if (readErr.IsNone() && mSpawner->IsAlive(existingPID) && IsDnsmasq(existingPID, storageDir)) {
        LOG_INF() << "Adopt running dnsmasq" << Log::Field("networkID", nidStr.c_str())
                  << Log::Field("pid", existingPID);

        pid = existingPID;
    } else {
        auto [newPID, spawnErr] = mSpawner->Spawn(cDnsmasqBinary, BuildArgs(storageDir, params));
        if (!spawnErr.IsNone()) {
            return {nullptr, AOS_ERROR_WRAP(spawnErr)};
        }

        pid = newPID;
    }

    Error err;

    auto rollback = DeferRelease(&err, [this, pid, &storageDir](const Error* e) {
        if (e->IsNone()) {
            return;
        }

        if (auto kerr = mSpawner->Kill(pid); !kerr.IsNone()) {
            LOG_ERR() << "Failed to kill dnsmasq on rollback" << Log::Field(kerr);
        }

        WipeStorageDir(storageDir);
    });

    Entry entry;

    entry.mPID    = pid;
    entry.mServer = std::make_unique<DNSServer>();

    if (err = entry.mServer->Init(nidStr, storageDir, *mSpawner, pid); !err.IsNone()) {
        return {nullptr, err};
    }

    DNSServerItf* handle = entry.mServer.get();

    mServers.emplace(nidStr, std::move(entry));

    return {handle, ErrorEnum::eNone};
}

Error DNSName::RemoveServer(const String& networkID)
{
    LOG_DBG() << "Remove DNS server" << Log::Field("networkID", networkID);

    auto it = mServers.find(networkID.CStr());
    if (it == mServers.end()) {
        return ErrorEnum::eNone;
    }

    const auto pid        = it->second.mPID;
    const auto storageDir = StorageDirFor(networkID.CStr());

    mServers.erase(it);

    Error err;

    if (auto kerr = mSpawner->Kill(pid); !kerr.IsNone()) {
        LOG_ERR() << "Failed to kill dnsmasq" << Log::Field(kerr);
        err = AOS_ERROR_WRAP(kerr);
    }

    WipeStorageDir(storageDir);

    return err;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::string DNSName::StorageDirFor(const std::string& networkID) const
{
    return mDNSStoragePath + "/" + networkID;
}

Error DNSName::EnsureStorageDir(const std::string& dir) const
{
    std::error_code ec;

    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return Error(ErrorEnum::eRuntime, ec.message().c_str());
    }

    return ErrorEnum::eNone;
}

void DNSName::WipeStorageDir(const std::string& dir) const
{
    std::error_code ec;

    std::filesystem::remove_all(dir, ec);
    if (ec) {
        LOG_ERR() << "Failed to wipe DNS storage dir" << Log::Field("dir", dir.c_str())
                  << Log::Field("err", ec.message().c_str());
    }
}

std::vector<std::string> DNSName::BuildArgs(const std::string& storageDir, const DNSServerParams& params) const
{
    return {
        "--keep-in-foreground",
        "--no-hosts",
        "--no-resolv",
        "--resolv-file=/etc/resolv.conf",
        "--bind-interfaces",
        std::string("--interface=") + params.mBridgeIfName.CStr(),
        std::string("--listen-address=") + params.mBridgeIP.CStr(),
        "--addn-hosts=" + storageDir + "/" + cHostsFileName,
        "--pid-file=" + storageDir + "/" + cPidFileName,
        "--conf-file=/dev/null",
    };
}

bool DNSName::IsDnsmasq(Poco::Process::PID pid, const std::string& storageDir) const
{
    auto [cmdline, err] = mSpawner->GetCmdLine(pid);
    if (!err.IsNone()) {
        return false;
    }

    const auto marker = std::string("--pid-file=") + storageDir + "/" + cPidFileName;

    return cmdline.find(marker) != std::string::npos;
}

RetWithError<Poco::Process::PID> DNSName::ReadPidFile(const std::string& storageDir) const
{
    const auto path = storageDir + "/" + cPidFileName;

    std::ifstream file(path);
    if (!file.is_open()) {
        return {0, Error(ErrorEnum::eNotFound, "pidfile not found")};
    }

    std::string content;
    std::getline(file, content);
    Poco::trimInPlace(content);

    if (content.empty()) {
        return {0, Error(ErrorEnum::eNotFound, "pidfile is empty")};
    }

    try {
        return {static_cast<Poco::Process::PID>(Poco::NumberParser::parse(content)), ErrorEnum::eNone};
    } catch (const Poco::Exception&) {
        return {0, Error(ErrorEnum::eInvalidArgument, "invalid pidfile content")};
    }
}

} // namespace aos::sm::networkmanager
