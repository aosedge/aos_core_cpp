/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOGPROVIDER_HPP_
#define LOGPROVIDER_HPP_

#include <aos/sm/logprovider.hpp>

namespace aos::sm::logprovider {

/**
 * Log provider interface.
 */
class LogProvider : public LogProviderItf {
public:
    /**
     * Gets instance log.
     *
     * @param request request log.
     * @return Error.
     */
    Error GetInstanceLog(const cloudprotocol::RequestLog& request) override
    {
        (void)request;

        return ErrorEnum::eNone;
    }

    /**
     * Gets instance crash log.
     *
     * @param request request log.
     * @return Error.
     */
    Error GetInstanceCrashLog(const cloudprotocol::RequestLog& request) override
    {
        (void)request;

        return ErrorEnum::eNone;
    }

    /**
     * Gets system log.
     *
     * @param request request log.
     * @return Error.
     */
    Error GetSystemLog(const cloudprotocol::RequestLog& request) override
    {
        (void)request;

        return ErrorEnum::eNone;
    }

    /**
     * Subscribes on logs.
     *
     * @param observer logs observer.
     * @return Error.
     */
    Error Subscribe(LogObserverItf& observer) override
    {
        (void)observer;

        return ErrorEnum::eNone;
    }

    /**
     * Unsubscribes from logs.
     *
     * @param observer logs observer.
     * @return Error.
     */
    Error Unsubscribe(LogObserverItf& observer) override
    {
        (void)observer;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::sm::logprovider

#endif
