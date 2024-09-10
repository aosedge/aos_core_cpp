/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WSPENDINGREQUESTS_HPP_
#define WSPENDINGREQUESTS_HPP_

#include <memory>
#include <mutex>
#include <vector>

#include <Poco/Event.h>

#include <utils/time.hpp>

/**
 * Request Params.
 */
class RequestParams {
public:
    /**
     * Creates Request Params instance.
     *
     * @param requestId request id.
     */
    explicit RequestParams(const std::string& requestId);

    /**
     * Sets response and event.
     *
     * @param requestId request id.
     */
    void SetResponse(const std::string& response);

    /**
     * Returns request id.
     *
     * @return const std::string&.
     */
    const std::string& GetRequestId() const;

    /**
     * Blocks up to timeout milliseconds waiting for response to be set.
     *
     * @param result[out] contains response value on success.
     * @param timeout wait timeout.
     *
     * @return bool - true if response was set within specified timeout.
     */
    bool TryWaitForResponse(std::string& result, const aos::common::utils::Duration timeout);

    /**
     * Compares request params.
     *
     * @param rhs request param to compare with.
     * @return bool.
     */
    bool operator<(const RequestParams& rhs) const;

private:
    std::string mRequestId;
    std::string mResponse;
    Poco::Event mEvent;
};

using RequestParamsPtr = std::shared_ptr<RequestParams>;

/**
 * Pending Requests.
 */
class PendingRequests {
public:
    /**
     * Add request
     *
     * @param requestParamsPtr request params pointer.
     */
    void Add(RequestParamsPtr requestParamsPtr);

    /**
     * Remove request
     *
     * @param requestId request id.
     */
    void Remove(RequestParamsPtr requestParamsPtr);

    /**
     * Set request response
     *
     * @param requestId request id.
     * @param response response.
     */
    bool SetResponse(const std::string& requestId, const std::string& response);

private:
    std::mutex                    mMutex;
    std::vector<RequestParamsPtr> mRequests;
};

#endif
