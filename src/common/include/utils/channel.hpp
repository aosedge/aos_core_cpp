/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UTILS_CHANNEL_HPP
#define UTILS_CHANNEL_HPP

#include <condition_variable>
#include <mutex>
#include <queue>

#include <aos/common/tools/error.hpp>

namespace aos::common::utils {

/**
 * Channel class.
 *
 * @tparam T type of the channel.
 */
template <typename T>
class Channel {
public:
    /**
     * Constructor.
     *
     * @param capacity channel capacity.
     */
    Channel(size_t capacity = 1)
        : mCapacity(capacity)
    {
    }

    /**
     * Send value to the channel.
     *
     * @param value value to send.
     * @return aos::Error.
     */
    Error Send(T value)
    {
        std::unique_lock<std::mutex> lock(mMutex);

        mCond.wait(lock, [this] { return mQueue.size() < mCapacity || mExit; });
        if (mExit) {
            return ErrorEnum::eWrongState;
        }

        mQueue.push(std::move(value));
        mCond.notify_one();

        return ErrorEnum::eNone;
    }

    /**
     * Receive value from the channel.
     *
     * @return RetWithError<T>.
     */
    RetWithError<T> Receive()
    {
        std::unique_lock<std::mutex> lock(mMutex);

        mCond.wait(lock, [this] { return !mQueue.empty() || mExit; });
        if (mExit) {
            return {{}, ErrorEnum::eWrongState};
        }

        auto value = std::move(mQueue.front());
        mQueue.pop();

        mCond.notify_one();

        return value;
    }

    /**
     * Close the channel.
     */
    void Close()
    {
        std::unique_lock<std::mutex> lock(mMutex);

        mExit = true;
        mCond.notify_all();
    }

private:
    bool                    mExit {};
    size_t                  mCapacity {};
    std::mutex              mMutex;
    std::condition_variable mCond;
    std::queue<T>           mQueue;
};

} // namespace aos::common::utils

#endif // UTILS_CHANNEL_HPP
