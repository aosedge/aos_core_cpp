/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_LOGPROVIDER_ARCHIVATOR_HPP_
#define AOS_COMMON_LOGPROVIDER_ARCHIVATOR_HPP_

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <Poco/DeflatingStream.h>

#include <core/common/logging/config.hpp>
#include <core/common/logging/itf/sender.hpp>
#include <core/common/types/log.hpp>

namespace aos::common::logging {

/**
 * Log archiver class.
 */
class Archiver {
public:
    /**
     * Constructor.
     *
     * @param logSender log sender.
     * @param config logging config.
     */
    Archiver(aos::logging::SenderItf& logSender, const aos::logging::Config& config);

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
     * @param correlationID correlation ID.
     * @return Error.
     */
    Error SendLog(const String& correlationID);

private:
    void  CreateCompressionStream();
    Error AddLogPart();

    aos::logging::SenderItf& mLogSender;
    aos::logging::Config     mConfig;

    size_t                                       mPartCount = {};
    size_t                                       mPartSize  = {};
    std::vector<std::ostringstream>              mLogStreams;
    std::unique_ptr<Poco::DeflatingOutputStream> mCompressionStream;
};

} // namespace aos::common::logging

#endif
