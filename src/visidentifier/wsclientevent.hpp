/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WSCLIENTEVENT_HPP_
#define WSCLIENTEVENT_HPP_

#include <string>

#include <Poco/Event.h>

/**
 * Web socket client event.
 */
class WSClientEvent {
public:
    /**
     * Web socket client event enum.
     */
    enum class EventEnum { CLOSED, FAILED };

    struct Details {
        EventEnum   mCode;
        std::string mMessage;
    };

    /**
     * Waits for event is to be set.
     *
     * @returns Details
     */
    Details Wait();

    /**
     * Sets event with the passed details.
     *
     * @param code event enum value
     * @param message event message
     * @returns std::pair<EventEnum, std::string>
     */
    void Set(const EventEnum code, const std::string& message);

    /**
     * Resets event.
     */
    void Reset();

private:
    Poco::Event mEvent;
    Details     mDetails;
};

#endif
