/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "retry.hpp"

namespace aos::common::utils {

Error Retry(const std::function<Error()>& retryFunc, const std::function<void(int, Duration, Error)> retryCbk,
    int maxTry, Duration delay, Duration maxDelay)
{
    CancelContext ctx;

    return Retry(ctx, retryFunc, retryCbk, maxTry, delay, maxDelay);
}

Error Retry(CancelContext& ctx, const std::function<Error()>& retryFunc,
    const std::function<void(int, Duration, Error)>& retryCbk, int maxTry, Duration delay, Duration maxDelay)
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

        if (auto waitErr = ctx.Wait(delay); !waitErr.Is(ErrorEnum::eTimeout)) {
            return waitErr;
        }

        delay *= 2;

        if (maxDelay && delay > maxDelay) {
            delay = maxDelay;
        }

        attempt++;
    }

    return err;
}

} // namespace aos::common::utils
