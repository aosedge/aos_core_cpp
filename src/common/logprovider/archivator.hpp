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

#include <core/common/cloudprotocol/log.hpp>
#include <core/common/logprovider/config.hpp>
#include <core/sm/logprovider/logprovider.hpp>

namespace aos::common::logprovider {

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
    Archivator(sm::logprovider::LogObserverItf& logReceiver, const aos::logprovider::Config& config);

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
    void  CreateCompressionStream();
    Error AddLogPart();

    sm::logprovider::LogObserverItf& mLogReceiver;
    aos::logprovider::Config         mConfig;

    uint64_t                                     mPartCount = {};
    uint64_t                                     mPartSize  = {};
    std::vector<std::ostringstream>              mLogStreams;
    std::unique_ptr<Poco::DeflatingOutputStream> mCompressionStream;
};

} // namespace aos::common::logprovider

#endif
