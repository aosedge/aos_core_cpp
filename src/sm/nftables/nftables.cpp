/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

extern "C" {
#include <nftables/libnftables.h>
}

#include <regex>
#include <sstream>
#include <utility>

#include <core/common/tools/logger.hpp>

#include "nftables.hpp"

namespace aos::sm::nftables {

namespace {

class NFTCtxGuard {
public:
    NFTCtxGuard()
        : mCtx(nft_ctx_new(NFT_CTX_DEFAULT))
    {
        if (mCtx != nullptr) {
            nft_ctx_buffer_error(mCtx);
            nft_ctx_buffer_output(mCtx);
            nft_ctx_output_set_flags(mCtx, nft_ctx_output_get_flags(mCtx) | NFT_CTX_OUTPUT_HANDLE);
        }
    }

    ~NFTCtxGuard()
    {
        if (mCtx != nullptr) {
            nft_ctx_free(mCtx);
        }
    }

    NFTCtxGuard(const NFTCtxGuard&)            = delete;
    NFTCtxGuard& operator=(const NFTCtxGuard&) = delete;

    nft_ctx* Get() const { return mCtx; }

    std::string ErrorBuffer() const
    {
        const char* err = mCtx != nullptr ? nft_ctx_get_error_buffer(mCtx) : nullptr;

        return err != nullptr ? std::string {err} : std::string {};
    }

    std::string OutputBuffer() const
    {
        const char* out = mCtx != nullptr ? nft_ctx_get_output_buffer(mCtx) : nullptr;

        return out != nullptr ? std::string {out} : std::string {};
    }

private:
    nft_ctx* mCtx;
};

bool IsNotFoundError(const std::string& err)
{
    return err.find("No such file or directory") != std::string::npos;
}

bool HasUnsafeToken(const std::string& value)
{
    return value.find_first_of(" \t\n\r;\"{}\\") != std::string::npos;
}

bool RuleHasUnsafeToken(const FWRule& rule)
{
    return HasUnsafeToken(rule.mSrcAddr) || HasUnsafeToken(rule.mDstAddr) || HasUnsafeToken(rule.mProto)
        || HasUnsafeToken(rule.mOIFName) || HasUnsafeToken(rule.mJumpTarget) || HasUnsafeToken(rule.mCtState);
}

void AppendRuleExpr(std::ostringstream& buf, const FWRule& rule)
{
    if (!rule.mCtState.empty()) {
        buf << " ct state " << rule.mCtState;
    }

    if (!rule.mSrcAddr.empty()) {
        buf << " ip saddr " << rule.mSrcAddr;
    }

    if (!rule.mDstAddr.empty()) {
        buf << " ip daddr " << rule.mDstAddr;
    }

    if (!rule.mProto.empty()) {
        buf << " " << rule.mProto;

        if (rule.mDstPort != 0) {
            buf << " dport " << rule.mDstPort;
        }
    }

    if (!rule.mOIFName.empty()) {
        buf << " oifname " << (rule.mOIFNeg ? "!= " : "") << "\"" << rule.mOIFName << "\"";
    }

    if (rule.mCounter) {
        buf << " counter";
    }

    switch (rule.mAction.GetValue()) {
    case FWActionEnum::eAccept:
        buf << " accept";
        break;

    case FWActionEnum::eDrop:
        buf << " drop";
        break;

    case FWActionEnum::eJump:
        buf << " jump " << rule.mJumpTarget;
        break;

    case FWActionEnum::eMasquerade:
        buf << " masquerade";
        break;

    case FWActionEnum::eReturn:
        buf << " return";
        break;
    }
}

bool ParseRuleLine(const std::string& line, FWListedRule& out)
{
    static const std::regex ctStateRe(R"(ct\s+state\s+([a-z,]+))");
    static const std::regex saddrRe(R"(ip\s+saddr\s+(\S+))");
    static const std::regex daddrRe(R"(ip\s+daddr\s+(\S+))");
    static const std::regex dportRe(R"((tcp|udp)\s+dport\s+(\d+))");
    static const std::regex protoRe(R"(\b(tcp|udp)\b)");
    static const std::regex oifnameRe(R"rx(oifname\s+(!=\s+)?"([^"]+)")rx");
    static const std::regex counterRe(R"(counter\s+packets\s+(\d+)\s+bytes\s+(\d+))");
    static const std::regex jumpRe(R"(\bjump\s+(\S+))");
    static const std::regex masqueradeRe(R"(\bmasquerade\b)");
    static const std::regex acceptRe(R"(\baccept\b)");
    static const std::regex dropRe(R"(\bdrop\b)");
    static const std::regex returnRe(R"(\breturn\b)");
    static const std::regex handleRe(R"(#\s+handle\s+(\d+))");

    std::smatch m;

    if (std::regex_search(line, m, ctStateRe)) {
        out.mRule.mCtState = m[1];
    }

    if (std::regex_search(line, m, saddrRe)) {
        out.mRule.mSrcAddr = m[1];
    }

    if (std::regex_search(line, m, daddrRe)) {
        out.mRule.mDstAddr = m[1];
    }

    if (std::regex_search(line, m, dportRe)) {
        out.mRule.mProto   = m[1];
        out.mRule.mDstPort = static_cast<uint16_t>(std::stoi(m[2]));
    } else if (std::regex_search(line, m, protoRe)) {
        out.mRule.mProto = m[1];
    }

    if (std::regex_search(line, m, oifnameRe)) {
        out.mRule.mOIFNeg  = m[1].matched;
        out.mRule.mOIFName = m[2];
    }

    if (std::regex_search(line, m, counterRe)) {
        out.mRule.mCounter = true;
        out.mPackets       = std::stoull(m[1]);
        out.mBytes         = std::stoull(m[2]);
    }

    bool actionFound = false;

    if (std::regex_search(line, m, jumpRe)) {
        out.mRule.mAction     = FWActionEnum::eJump;
        out.mRule.mJumpTarget = m[1];
        actionFound           = true;
    } else if (std::regex_search(line, m, masqueradeRe)) {
        out.mRule.mAction = FWActionEnum::eMasquerade;
        actionFound       = true;
    } else if (std::regex_search(line, m, acceptRe)) {
        out.mRule.mAction = FWActionEnum::eAccept;
        actionFound       = true;
    } else if (std::regex_search(line, m, dropRe)) {
        out.mRule.mAction = FWActionEnum::eDrop;
        actionFound       = true;
    } else if (std::regex_search(line, m, returnRe)) {
        out.mRule.mAction = FWActionEnum::eReturn;
        actionFound       = true;
    }

    if (!std::regex_search(line, m, handleRe)) {
        return false;
    }

    out.mHandle = static_cast<FWRuleHandle>(std::stoull(m[1]));

    return actionFound;
}

} // namespace

/***********************************************************************************************************************
 * NFTxn
 **********************************************************************************************************************/

class NFTables::NFTxn : public FWTxnItf {
public:
    NFTxn(NFTables& parent, std::string family)
        : mParent(parent)
        , mFamily(std::move(family))
    {
    }

    void AddTable(const std::string& table) override { mBuf << "add table " << mFamily << " " << table << "\n"; }

    void DeleteTable(const std::string& table) override { mBuf << "delete table " << mFamily << " " << table << "\n"; }

    void AddBaseChain(const FWBaseChain& chain) override
    {
        mBuf << "add chain " << mFamily << " " << chain.mTable << " " << chain.mName << " { type "
             << chain.mType.ToString().CStr() << " hook " << chain.mHook.ToString().CStr() << " priority "
             << chain.mPriority << "; policy " << chain.mPolicy.ToString().CStr() << "; }\n";
    }

    void AddChain(const FWChain& chain) override
    {
        mBuf << "add chain " << mFamily << " " << chain.mTable << " " << chain.mName << "\n";
    }

    void FlushChain(const std::string& table, const std::string& chain) override
    {
        mBuf << "flush chain " << mFamily << " " << table << " " << chain << "\n";
    }

    void DeleteChain(const std::string& table, const std::string& chain) override
    {
        mBuf << "delete chain " << mFamily << " " << table << " " << chain << "\n";
    }

    Error AddRule(const std::string& table, const std::string& chain, const FWRule& rule) override
    {
        if (RuleHasUnsafeToken(rule)) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "unsafe token in nft rule field"));
        }

        mBuf << "add rule " << mFamily << " " << table << " " << chain;

        AppendRuleExpr(mBuf, rule);

        mBuf << "\n";

        return ErrorEnum::eNone;
    }

    void DeleteRuleByHandle(const std::string& table, const std::string& chain, FWRuleHandle handle) override
    {
        mBuf << "delete rule " << mFamily << " " << table << " " << chain << " handle " << handle << "\n";
    }

    Error Commit() override
    {
        const auto cmd = mBuf.str();

        mBuf.str(std::string {});
        mBuf.clear();

        if (cmd.empty()) {
            return ErrorEnum::eNone;
        }

        return mParent.RunBuffer(cmd);
    }

    Error Commit(std::vector<FWRuleHandle>& addedHandles) override
    {
        const auto cmd = mBuf.str();

        mBuf.str(std::string {});
        mBuf.clear();

        if (cmd.empty()) {
            return ErrorEnum::eNone;
        }

        return mParent.RunBufferEcho(cmd, addedHandles);
    }

    Error Commit(std::vector<FWListedRule>& addedRules) override
    {
        const auto cmd = mBuf.str();

        mBuf.str(std::string {});
        mBuf.clear();

        if (cmd.empty()) {
            return ErrorEnum::eNone;
        }

        return mParent.RunBufferEchoRules(cmd, addedRules);
    }

private:
    NFTables&          mParent;
    std::string        mFamily;
    std::ostringstream mBuf;
};

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

NFTables::NFTables(std::string family)
    : mFamily(std::move(family))
{
}

std::unique_ptr<FWTxnItf> NFTables::NewTxn()
{
    return std::make_unique<NFTxn>(*this, mFamily);
}

Error NFTables::ListChainRules(const std::string& table, const std::string& chain, std::vector<FWListedRule>& out)
{
    std::ostringstream cmd;

    cmd << "list chain " << mFamily << " " << table << " " << chain;

    std::string output;

    if (auto err = RunBufferWithOutput(cmd.str(), output); !err.IsNone()) {
        return err;
    }

    std::istringstream iss(output);
    std::string        line;

    while (std::getline(iss, line)) {
        FWListedRule listed {};

        if (ParseRuleLine(line, listed)) {
            out.push_back(std::move(listed));
        }
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error NFTables::RunBuffer(const std::string& cmd)
{
    std::lock_guard<std::mutex> lock {mMutex};

    NFTCtxGuard ctx;
    if (ctx.Get() == nullptr) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "nft_ctx_new failed"));
    }

    if (nft_run_cmd_from_buffer(ctx.Get(), cmd.c_str()) != 0) {
        const auto errText = ctx.ErrorBuffer();

        if (IsNotFoundError(errText)) {
            return Error(ErrorEnum::eNotFound, errText.empty() ? "nftables object not found" : errText.c_str());
        }

        LOG_ERR() << "nftables command failed: " << cmd.c_str() << ", err=" << errText.c_str();

        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, errText.empty() ? "nftables command failed" : errText.c_str()));
    }

    return ErrorEnum::eNone;
}

Error NFTables::RunBufferWithOutput(const std::string& cmd, std::string& output)
{
    std::lock_guard<std::mutex> lock {mMutex};

    NFTCtxGuard ctx;
    if (ctx.Get() == nullptr) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "nft_ctx_new failed"));
    }

    if (nft_run_cmd_from_buffer(ctx.Get(), cmd.c_str()) != 0) {
        const auto errText = ctx.ErrorBuffer();

        if (IsNotFoundError(errText)) {
            return Error(ErrorEnum::eNotFound, errText.empty() ? "nftables object not found" : errText.c_str());
        }

        LOG_ERR() << "nftables command failed: " << cmd.c_str() << ", err=" << errText.c_str();

        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, errText.empty() ? "nftables command failed" : errText.c_str()));
    }

    output = ctx.OutputBuffer();

    return ErrorEnum::eNone;
}

Error NFTables::RunBufferEcho(const std::string& cmd, std::vector<FWRuleHandle>& handles)
{
    std::lock_guard<std::mutex> lock {mMutex};

    NFTCtxGuard ctx;
    if (ctx.Get() == nullptr) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "nft_ctx_new failed"));
    }

    nft_ctx_output_set_flags(ctx.Get(), nft_ctx_output_get_flags(ctx.Get()) | NFT_CTX_OUTPUT_ECHO);

    if (nft_run_cmd_from_buffer(ctx.Get(), cmd.c_str()) != 0) {
        const auto errText = ctx.ErrorBuffer();

        if (IsNotFoundError(errText)) {
            return Error(ErrorEnum::eNotFound, errText.empty() ? "nftables object not found" : errText.c_str());
        }

        LOG_ERR() << "nftables command failed" << Log::Field("cmd", cmd.c_str()) << Log::Field("err", errText.c_str());

        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, errText.empty() ? "nftables command failed" : errText.c_str()));
    }

    const auto         output = ctx.OutputBuffer();
    std::istringstream iss(output);
    std::string        line;
    const std::regex   re(R"(#\s+handle\s+(\d+))");

    while (std::getline(iss, line)) {
        std::smatch m;

        if (std::regex_search(line, m, re)) {
            handles.push_back(static_cast<FWRuleHandle>(std::stoull(m[1])));
        }
    }

    return ErrorEnum::eNone;
}

Error NFTables::RunBufferEchoRules(const std::string& cmd, std::vector<FWListedRule>& rules)
{
    std::lock_guard<std::mutex> lock {mMutex};

    NFTCtxGuard ctx;
    if (ctx.Get() == nullptr) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "nft_ctx_new failed"));
    }

    nft_ctx_output_set_flags(ctx.Get(), nft_ctx_output_get_flags(ctx.Get()) | NFT_CTX_OUTPUT_ECHO);

    if (nft_run_cmd_from_buffer(ctx.Get(), cmd.c_str()) != 0) {
        const auto errText = ctx.ErrorBuffer();

        if (IsNotFoundError(errText)) {
            return Error(ErrorEnum::eNotFound, errText.empty() ? "nftables object not found" : errText.c_str());
        }

        LOG_ERR() << "nftables command failed" << Log::Field("cmd", cmd.c_str()) << Log::Field("err", errText.c_str());

        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, errText.empty() ? "nftables command failed" : errText.c_str()));
    }

    const auto         output = ctx.OutputBuffer();
    std::istringstream iss(output);
    std::string        line;
    const std::regex   jumpRe(R"(\bjump\s+(\S+))");
    const std::regex   handleRe(R"(#\s+handle\s+(\d+))");

    while (std::getline(iss, line)) {
        std::smatch jm;
        if (!std::regex_search(line, jm, jumpRe)) {
            continue;
        }

        std::smatch hm;
        if (!std::regex_search(line, hm, handleRe)) {
            continue;
        }

        FWListedRule listed {};
        listed.mRule.mAction     = FWActionEnum::eJump;
        listed.mRule.mJumpTarget = jm[1];
        listed.mHandle           = static_cast<FWRuleHandle>(std::stoull(hm[1]));

        rules.push_back(std::move(listed));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::nftables
