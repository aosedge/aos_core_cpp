/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ARCHIVATOR_HPP_
#define ARCHIVATOR_HPP_

#include <string>

#include <Poco/DeflatingStream.h>

#include <aos/common/cloudprotocol/cloudprotocol.hpp>
#include <aos/common/cloudprotocol/log.hpp>
#include <aos/sm/logprovider.hpp>
#include <config/config.hpp>

namespace aos::sm::logprovider {

/**
 * Log Archivator class.
 */
class Archivator {
public:
    /**
     * Constructor.
     *
     * @param logReceiver log receiver.
     * @param config logprovider config.
     */
    Archivator(LogObserverItf& logReceiver, const config::LoggingConfig& config);

    /**
     * Adds log message to the archivator.
     *
     * @param message The log message to be added.
     * @return Error.
     */
    Error AddLog(const std::string& message);

    /**
     * Sends accumulated log parts to the listener.
     *
     * @param logID log ID.
     * @return Error.
     */
    Error SendLog(const StaticString<cloudprotocol::cLogIDLen>& logID);

private:
    void CreateCompressionStream();
    bool AddLogPart();

    LogObserverItf&       mLogReceiver;
    config::LoggingConfig mConfig;

    uint64_t                                     mPartCount;
    uint64_t                                     mPartSize;
    std::vector<std::ostringstream>              mLogStreams;
    std::unique_ptr<Poco::DeflatingOutputStream> mCompressionStream;
};

} // namespace aos::sm::logprovider

#endif // ARCHIVATOR_HPP_
