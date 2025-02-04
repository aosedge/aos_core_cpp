/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <memory>
#include <regex>
#include <sstream>

#include "network/iptables.hpp"
#include "utils/exception.hpp"

namespace aos::common::network {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RuleBuilder& RuleBuilder::Source(const std::string& addr)
{
    if (addr.empty()) {
        return *this;
    }

    mRule += "-s " + addr + " ";

    return *this;
}

RuleBuilder& RuleBuilder::Destination(const std::string& addr)
{
    if (addr.empty()) {
        return *this;
    }

    mRule += "-d " + addr + " ";

    return *this;
}

RuleBuilder& RuleBuilder::Protocol(const std::string& proto)
{
    mRule += "-p " + proto + " ";

    return *this;
}

RuleBuilder& RuleBuilder::Jump(const std::string& target)
{
    if (target.empty()) {
        return *this;
    }

    mRule += "-j " + target;

    return *this;
}

RuleBuilder& RuleBuilder::SourcePort(uint16_t port)
{
    if (port == 0) {
        return *this;
    }

    mRule += "--sport " + std::to_string(port) + " ";

    return *this;
}

RuleBuilder& RuleBuilder::DestinationPort(uint16_t port)
{
    if (port == 0) {
        return *this;
    }

    mRule += "--dport " + std::to_string(port) + " ";

    return *this;
}

std::string RuleBuilder::Build() const
{
    return mRule;
}

void RuleBuilder::Reset()
{
    mRule.clear();
}

IPTables::IPTables(const std::string& table)
    : mTable(table)
{
}

Error IPTables::Append(const std::string& chain, const RuleBuilder& builder)
{
    std::lock_guard<std::mutex> lock {mMutex};

    try {
        std::string        ruleSpec = builder.Build();
        std::ostringstream command;

        command << "iptables -t " << mTable << " -A " << chain << " " << ruleSpec;

        ExecuteCommand(command.str());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

RetWithError<std::vector<std::string>> IPTables::ListAllRulesWithCounters(const std::string& chain)
{
    std::lock_guard<std::mutex> lock {mMutex};

    std::vector<std::string> rules;

    try {
        std::ostringstream command;

        command << "iptables -t " << mTable << " -v -S " << chain;

        rules = ExecuteCommandWithOutput(command.str());

    } catch (const std::exception& e) {
        return {{}, AOS_ERROR_WRAP(utils::ToAosError(e))};
    }

    return rules;
}

Error IPTables::Insert(const std::string& chain, unsigned int position, const RuleBuilder& builder)
{
    std::lock_guard<std::mutex> lock {mMutex};

    try {
        std::string        ruleSpec = builder.Build();
        std::ostringstream command;

        command << "iptables -t " << mTable << " -I " << chain << " " << position << " " << ruleSpec;

        ExecuteCommand(command.str());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error IPTables::DeleteRule(const std::string& chain, const RuleBuilder& builder)
{
    std::lock_guard<std::mutex> lock {mMutex};

    try {
        std::string        ruleSpec = builder.Build();
        std::ostringstream command;

        command << "iptables -t " << mTable << " -D " << chain << " " << ruleSpec;

        ExecuteCommand(command.str());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error IPTables::NewChain(const std::string& chain)
{
    std::lock_guard<std::mutex> lock {mMutex};

    try {
        std::ostringstream command;

        command << "iptables -t " << mTable << " -N " << chain;

        ExecuteCommand(command.str());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error IPTables::ClearChain(const std::string& chain)
{
    std::lock_guard<std::mutex> lock {mMutex};

    try {
        std::ostringstream command;

        command << "iptables -t " << mTable << " -F " << chain;

        ExecuteCommand(command.str());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error IPTables::DeleteChain(const std::string& chain)
{
    std::lock_guard<std::mutex> lock {mMutex};

    try {
        std::ostringstream command;

        command << "iptables -t " << mTable << " -X " << chain;

        ExecuteCommand(command.str());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

RetWithError<std::vector<std::string>> IPTables::ListChains()
{
    std::lock_guard<std::mutex> lock {mMutex};

    std::vector<std::string> chains;

    try {
        std::ostringstream command;

        command << "iptables -t " << mTable << " -L -n";

        auto output = ExecuteCommandWithOutput(command.str());

        for (const auto& line : output) {
            if (line.find("Chain") == 0) {
                // Extract only the chain name (the word after "Chain")
                size_t start = line.find("Chain") + 6;
                size_t end   = line.find(' ', start);

                chains.push_back(line.substr(start, end - start));
            }
        }
    } catch (const std::exception& e) {
        return {{}, AOS_ERROR_WRAP(utils::ToAosError(e))};
    }

    return chains;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void IPTables::ExecuteCommand(const std::string& command) const
{
    if (int result = std::system(command.c_str()); result != 0) {
        throw std::runtime_error("failed to execute iptables command: " + command);
    }
}

std::vector<std::string> IPTables::ExecuteCommandWithOutput(const std::string& command) const
{
    std::vector<std::string> output;

    // this lambda is used to suppress the warning about ignoring the return value
    auto closePipe = [](FILE* pipe) { pclose(pipe); };

    std::unique_ptr<FILE, decltype(closePipe)> pipe(popen(command.c_str(), "r"), closePipe);

    if (!pipe) {
        throw std::runtime_error("failed to execute command: " + command);
    }

    std::array<char, 128> buffer;

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output.emplace_back(buffer.data());
    }

    return output;
}

} // namespace aos::common::network
