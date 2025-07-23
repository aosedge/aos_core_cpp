/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstring>
#include <errno.h>
#include <fstream>
#include <map>
#include <signal.h>

#include <Poco/Exception.h>
#include <Poco/File.h>
#include <Poco/NumberParser.h>
#include <Poco/Process.h>
#include <Poco/String.h>

#include <common/utils/exception.hpp>

#include "dnsserver.hpp"

namespace aos::cm::networkmanager {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

void DNSServer::Init(const std::string& dnsStoragePath, const std::string& IP)
{
    mDnsStoragePath = dnsStoragePath;
    mIP             = IP;
}

Error DNSServer::UpdateHostsFile(const HostsMap& hosts)
{
    auto hostsFilePath = mDnsStoragePath + "/" + cHostFileName;

    std::ofstream file(hostsFilePath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return Error(ErrorEnum::eRuntime, "failed to open hosts file");
    }

    for (const auto& [ip, hostNames] : hosts) {
        std::string entry = ip;

        for (const auto& hostName : hostNames) {
            entry += "\t" + hostName;
        }

        entry += "\n";

        file << entry;
        if (file.fail()) {
            return Error(ErrorEnum::eRuntime, "failed to write to hosts file");
        }
    }

    return ErrorEnum::eNone;
}

std::string DNSServer::GetIP() const
{
    return mIP;
}

Error DNSServer::Restart()
{
    try {
        RestartProcess(FindServerProcess());
    } catch (const std::runtime_error& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Poco::Process::PID DNSServer::FindServerProcess()
{
    auto pidFilePath = mDnsStoragePath + "/" + cPidFileName;

    std::ifstream file(pidFilePath);
    if (!file.is_open()) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "failed to open PID file");
    }

    std::string pidContent;
    std::getline(file, pidContent);

    if (pidContent.empty()) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "process not exist - PID file is empty");
    }

    Poco::trimInPlace(pidContent);

    Poco::Process::PID pid;

    try {
        pid = Poco::NumberParser::parse(pidContent);
    } catch (const Poco::Exception& e) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "invalid PID format: " + pidContent);
    }

    if (!Poco::Process::isRunning(pid)) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "process not found: " + std::to_string(pid));
    }

    return pid;
}

void DNSServer::RestartProcess(Poco::Process::PID pid)
{
    if (::kill(pid, SIGHUP) != 0) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "failed to send SIGHUP signal: " + std::string(strerror(errno)));
    }
}

} // namespace aos::cm::networkmanager
