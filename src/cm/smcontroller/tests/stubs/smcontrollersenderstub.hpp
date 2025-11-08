/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_SMCONTROLLER_SENDERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_SMCONTROLLER_SENDERSTUB_HPP_

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <core/cm/smcontroller/itf/sender.hpp>

namespace aos::cm::smcontroller {

/**
 * Log sender stub.
 */
class SenderStub : public SenderItf {
public:
    Error SendLog(const PushLog& log) override
    {
        std::lock_guard lock {mMutex};

        mLogs.push_back(log);
        mCV.notify_one();

        return ErrorEnum::eNone;
    }

    Error WaitLog(const String& correlationID, uint64_t part)
    {
        std::unique_lock lock {mMutex};

        auto hasLog = [this, &correlationID, part]() {
            auto it = std::find_if(mLogs.begin(), mLogs.end(), [&correlationID, part](const PushLog& log) {
                return log.mCorrelationID == correlationID && log.mPart == part;
            });

            return it != mLogs.end();
        };

        bool received = mCV.wait_for(lock, cDefaultTimeout, hasLog);

        if (!received) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait log timeout"));
        }

        return ErrorEnum::eNone;
    }

    bool HasLog(const String& correlationID, uint64_t part) const
    {
        std::lock_guard lock {mMutex};

        auto it = std::find_if(mLogs.begin(), mLogs.end(), [&correlationID, part](const PushLog& log) {
            return log.mCorrelationID == correlationID && log.mPart == part;
        });

        return it != mLogs.end();
    }

private:
    static constexpr auto cDefaultTimeout = std::chrono::seconds(1);

    std::vector<PushLog>    mLogs;
    mutable std::mutex      mMutex;
    std::condition_variable mCV;
};

} // namespace aos::cm::smcontroller

#endif
