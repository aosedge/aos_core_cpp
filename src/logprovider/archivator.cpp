/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Zip/Compress.h>

#include <logger/logmodule.hpp>

#include "archivator.hpp"

namespace aos::sm::logprovider {

Archivator::Archivator(LogObserverItf& logReceiver, const config::LoggingConfig& config)
    : mLogReceiver(logReceiver)
    , mConfig(config)
    , mPartCount(0)
    , mPartSize(0)
{
    CreateCompressionStream();
}

Error Archivator::AddLog(const std::string& message)
{
    if (mPartCount >= mConfig.mMaxPartCount) {
        return AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument);
    }

    *mCompressionStream << message;

    mPartSize += message.size();
    if (mPartSize > mConfig.mMaxPartSize) {
        if (!AddLogPart()) {
            return AOS_ERROR_WRAP(ErrorEnum::eFailed);
        }

        LOG_DBG() << "Max part size reached: partCount=" << mPartCount;
    }

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

        cloudprotocol::PushLog emptyLog;

        emptyLog.mMessageType = cloudprotocol::LogMessageTypeEnum::ePushLog;
        emptyLog.mLogID       = logID;
        emptyLog.mPartsCount  = part;
        emptyLog.mPart        = part;
        emptyLog.mStatus      = cloudprotocol::LogStatusEnum::eEmpty;

        mLogReceiver.OnLogReceived(emptyLog);

        return ErrorEnum::eNone;
    }

    for (size_t i = 0; i < mLogStreams.size(); ++i) {
        auto data = mLogStreams[i].str();
        auto part = i + 1;

        LOG_DBG() << "Push log: "
                  << "part=" << part << ", size=" << data.size();

        cloudprotocol::PushLog logPart;

        logPart.mMessageType = cloudprotocol::LogMessageTypeEnum::ePushLog;
        logPart.mLogID       = logID;
        logPart.mPartsCount  = mLogStreams.size();
        logPart.mPart        = part;
        logPart.mStatus      = cloudprotocol::LogStatusEnum::eOk;

        auto err = logPart.mContent.Insert(logPart.mContent.begin(), data.data(), data.data() + data.size());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mLogReceiver.OnLogReceived(logPart);
    }

    return ErrorEnum::eNone;
}
void Archivator::CreateCompressionStream()
{
    auto& stream = mLogStreams.emplace_back();

    mCompressionStream = std::make_unique<Poco::DeflatingOutputStream>(
        stream, Poco::DeflatingStreamBuf::STREAM_GZIP, Z_BEST_COMPRESSION);
}

bool Archivator::AddLogPart()
{
    try {
        mCompressionStream->close();

        mPartCount++;
        mPartSize = 0;

        CreateCompressionStream();
    } catch (const std::exception& ex) {
        return false;
    }

    return true;
}

} // namespace aos::sm::logprovider
