/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_SYNCMESSAGESENDER_HPP_
#define AOS_COMMON_UTILS_SYNCMESSAGESENDER_HPP_

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include <grpcpp/support/sync_stream.h>

#include <common/utils/exception.hpp>
#include <core/common/tools/error.hpp>
#include <core/common/tools/optional.hpp>

namespace aos::common::utils {

/**
 * Message structure for synchronous communication.
 */
template <typename Response>
struct SyncMessage {
    Response* mResponse {};
    bool      mResponseReceived {};
};

/**
 * Synchronous message sender over gRPC streams.
 *
 * @tparam Request request message type.
 * @tparam Response outgoing message type.
 */
template <typename Request, typename Response>
class SyncMessageSender {
public:
    /**
     * Initializes the sender with a gRPC stream, shared write mutex, and timeout.
     *
     * @param stream gRPC stream pointer.
     * @param writeMutex external mutex shared with all other writers on the same stream.
     * @param timeout response timeout.
     */
    void Init(grpc::ServerReaderWriter<Request, Response>* stream, std::mutex& writeMutex,
        std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        mStream     = stream;
        mWriteMutex = &writeMutex;
        mTimeout    = timeout;
    }

    /**
     * Sends a message synchronously and waits for response.
     *
     * @tparam Request request message type.
     * @param request incoming message to send.
     * @param response outgoing message to receive.
     * @return Error.
     */
    Error SendSync(const Request& request, Response& response)
    {
        if (!mStream) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stream not initialized"));
        }

        SyncMessage<Response> msg;

        msg.mResponse = &response;

        {
            std::lock_guard lock {mMutex};

            mMessages.push_back(&msg);
        }

        // Use the shared write mutex so that SendSync and SendMessage never
        // call mStream->Write() concurrently (gRPC forbids concurrent writes).
        {
            std::lock_guard writeLock {*mWriteMutex};

            if (!mStream->Write(request)) {
                std::lock_guard lock {mMutex};

                EraseMessage(&msg);

                return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to send message"));
            }
        }

        {
            std::unique_lock lock {mMutex};

            // Receiving the message.
            mCondVar.wait_for(lock, mTimeout, [&msg] { return msg.mResponseReceived; });

            EraseMessage(&msg);

            if (!msg.mResponseReceived) {
                return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "response timeout"));
            }
        }

        return ErrorEnum::eNone;
    }

    /**
     * Registers a message processing handler.
     *
     * @param checkFunc function to check if the Response matches the handler criteria.
     * @param copyFunc function to copy data from source Response to destination Response.
     * @param strict if true, ProcessResponse returs error if none of the input messages matches response.
     */
    void RegisterResponseHandler(
        std::function<bool(const Response&)> checkFunc, std::function<void(const Response&, Response&)> copyFunc)
    {
        std::lock_guard lock {mMutex};

        mResponseHandlers.emplace_back(ResponseHandler {std::move(checkFunc), std::move(copyFunc)});
    }

    /**
     * Processes a response message through the chain of registered handlers.
     *
     * @param outputMessage response message to process.
     * @return Optional<Error>.
     */
    Optional<Error> ProcessResponse(Response& outputMessage)
    {
        std::lock_guard lock {mMutex};

        for (auto& handler : mResponseHandlers) {
            // Check if this handler matches the incoming message
            if (!handler.mCheckFunc(outputMessage)) {
                continue;
            }

            // Find waiting messages that match and copy the response
            for (auto* msg : mMessages) {
                if (!msg->mResponse || !handler.mCheckFunc(*msg->mResponse)) {
                    continue;
                }

                try {
                    handler.mCopyFunc(outputMessage, *msg->mResponse);
                    msg->mResponseReceived = true;
                    mCondVar.notify_all();

                    return Error();
                } catch (const std::exception& e) {
                    return AOS_ERROR_WRAP(ToAosError(e));
                }
            }

            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "no matching request found"));
        }

        return Optional<Error>();
    }

private:
    struct ResponseHandler {
        std::function<bool(const Response&)>            mCheckFunc;
        std::function<void(const Response&, Response&)> mCopyFunc;
    };

    void EraseMessage(SyncMessage<Response>* msg)
    {
        auto it = std::find(mMessages.begin(), mMessages.end(), msg);
        if (it != mMessages.end()) {
            mMessages.erase(it);
        }
    }

    grpc::ServerReaderWriter<Request, Response>* mStream {};
    std::mutex*                                  mWriteMutex {};
    std::chrono::seconds                         mTimeout {5};
    std::vector<SyncMessage<Response>*>          mMessages;
    std::vector<ResponseHandler>                 mResponseHandlers;
    std::mutex                                   mMutex;
    std::condition_variable                      mCondVar;
};

} // namespace aos::common::utils

#endif
