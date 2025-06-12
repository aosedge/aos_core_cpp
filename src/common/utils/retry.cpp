/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>

#include "utils/retry.hpp"

namespace aos::common::utils {

Error Retry(const std::function<Error()>& retryFunc, const std::function<void(int, Duration, Error)> retryCbk,
    int maxTry, Duration delay, Duration maxDelay)
{
    Error err;
    auto  attempt = 1;

    for (;;) {
        if (err = retryFunc(); err.IsNone()) {
            return ErrorEnum::eNone;
        }

        if (attempt >= maxTry && maxTry != 0) {
            break;
        }

        if (retryCbk) {
            retryCbk(attempt, delay, err);
        }

        std::this_thread::sleep_for(std::chrono::duration<int64_t, std::nano>(delay.Nanoseconds()));

        delay *= 2;

        if (maxDelay && delay > maxDelay) {
            delay = maxDelay;
        }

        attempt++;
    }

    return err;
}

} // namespace aos::common::utils
