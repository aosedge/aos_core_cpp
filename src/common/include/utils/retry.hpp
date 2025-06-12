/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RETRY_HPP_
#define RETRY_HPP_

#include <functional>

#include <aos/common/tools/error.hpp>

#include "time.hpp"

namespace aos::common::utils {

/**
 * Retries function until it returns Error::eNone.
 *
 * @param retryFunc function to retry.
 * @param retryCbk callback function to call on each retry.
 * @param maxTry maximum number of retries, 0 - infinite.
 * @param delay initial delay between retries.
 * @param maxDelay maximum delay between retries.
 * @return Error
 */
Error Retry(const std::function<Error()>& retryFunc, const std::function<void(int, Duration, Error)> retryCbk = nullptr,
    int maxTry = 3, Duration delay = Time::cSeconds, Duration maxDelay = Time::cMinutes);

} // namespace aos::common::utils

#endif
