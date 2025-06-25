/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Zip/Compress.h>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>

#include "archivator.hpp"

namespace aos::common::logprovider {

Archivator::Archivator(sm::logprovider::LogObserverItf& logReceiver, const aos::logprovider::Config& config)
    : mLogReceiver(logReceiver)
    , mConfig(config)
{
    CreateCompressionStream();
}

Error Archivator::AddLog(const std::string& message)
{
    if (mPartCount >= mConfig.mMaxPartCount) {
        return AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument);
    }

    if (mPartSize + message.size() > mConfig.mMaxPartSize) {
        if (auto err = AddLogPart(); !err.IsNone()) {
            return err;
        }

        LOG_DBG() << "Max part size reached: partCount=" << mPartCount;
    }

    *mCompressionStream << message;
    mPartSize += message.size();

    return ErrorEnum::eNone;
}

Error Archivator::SendLog(const StaticString<cloudprotocol::cLogIDLen>& logID)
{
    mCompressionStream->close();

    if (mPartSize > 0) {
        mPartCount++;
    }

    if (mPartCount == 0) {
        auto part = 1;

        LOG_DBG() << "Push log: "
                  << "part=" << part << ", size=0";

        auto emptyLog = std::make_unique<cloudprotocol::PushLog>();

        emptyLog->mMessageType = cloudprotocol::LogMessageTypeEnum::ePushLog;
        emptyLog->mLogID       = logID;
        emptyLog->mPartsCount  = part;
        emptyLog->mPart        = part;
        emptyLog->mStatus      = cloudprotocol::LogStatusEnum::eEmpty;

        mLogReceiver.OnLogReceived(*emptyLog);

        return ErrorEnum::eNone;
    }

    for (size_t i = 0; i < mLogStreams.size(); ++i) {
        auto data = mLogStreams[i].str();
        auto part = i + 1;

        LOG_DBG() << "Push log: part=" << part << ", size=" << data.size();

        auto logPart = std::make_unique<cloudprotocol::PushLog>();

        logPart->mMessageType = cloudprotocol::LogMessageTypeEnum::ePushLog;
        logPart->mLogID       = logID;
        logPart->mPartsCount  = mLogStreams.size();
        logPart->mPart        = part;
        logPart->mStatus      = cloudprotocol::LogStatusEnum::eOk;

        auto err = logPart->mContent.Insert(logPart->mContent.begin(), data.data(), data.data() + data.size());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mLogReceiver.OnLogReceived(*logPart);
    }

    return ErrorEnum::eNone;
}
void Archivator::CreateCompressionStream()
{
    auto& stream = mLogStreams.emplace_back();

    if (mCompressionStream) {
        mCompressionStream->close();
    }

    mCompressionStream = std::make_unique<Poco::DeflatingOutputStream>(
        stream, Poco::DeflatingStreamBuf::STREAM_GZIP, Z_BEST_COMPRESSION);
}

Error Archivator::AddLogPart()
{
    try {
        mCompressionStream->close();

        mPartCount++;
        mPartSize = 0;

        CreateCompressionStream();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::logprovider
