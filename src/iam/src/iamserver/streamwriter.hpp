/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STREAMWRITER_HPP_
#define STREAMWRITER_HPP_

#include <iamanager/v5/iamanager.grpc.pb.h>

namespace aos::iam::iamserver {

/**
 * Controls writes to streams.
 */
template <typename T>
class StreamWriter {
public:
    /**
     * Closes all streams.
     */
    void Start()
    {
        std::lock_guard lock {mMutex};

        mIsRunning      = true;
        mNotificationID = 0;
    }

    /**
     * Closes all streams.
     */
    void Close()
    {
        {
            std::lock_guard lock {mMutex};

            mIsRunning = false;
            mLastMessage.reset();
        }

        mCV.notify_all();
    }

    /**
     * Writes notification message to all streams.
     *
     * @param message notification message.
     */
    void WriteToStreams(const T& message)
    {
        {
            std::lock_guard lock {mMutex};

            ++mNotificationID;
            mLastMessage = message;
        }

        mCV.notify_all();
    }

    /**
     * Handles stream. Blocks the caller until the stream is closed.
     *
     * @param context server context.
     * @param writer server writer.
     * @return grpc::Status.
     */
    grpc::Status HandleStream(grpc::ServerContext* context, grpc::ServerWriter<T>* writer)
    {
        uint32_t lastNotificationID = 0;

        while (mIsRunning && !context->IsCancelled()) {
            std::shared_lock lock {mMutex};

            bool timedOut = !mCV.wait_for(lock, cWaitTimeout, [this, lastNotificationID] {
                return (mNotificationID != lastNotificationID && mLastMessage.has_value()) || !mIsRunning;
            });

            if (!mIsRunning) {
                break;
            }

            if (timedOut) {
                continue;
            }

            // got notification, send it to the client
            if (!writer->Write(*mLastMessage)) {
                break;
            }

            lastNotificationID = mNotificationID;
        }

        return grpc::Status::OK;
    }

private:
    static constexpr auto cWaitTimeout = std::chrono::seconds(10);

    bool                        mIsRunning = true;
    std::condition_variable_any mCV;
    std::shared_mutex           mMutex;
    uint32_t                    mNotificationID = 0;
    std::optional<T>            mLastMessage;
};

/**
 * Sends certificate updates to GRPC streams.
 */
class CertWriter : public StreamWriter<iamanager::v5::CertInfo>, public aos::iam::certhandler::CertReceiverItf {
public:
    /**
     * CertWriter constructor.
     *
     * @param certType certificate type.
     */
    explicit CertWriter(const std::string& certType)
        : mCertType(certType)
    {
    }

private:
    void OnCertChanged(const aos::iam::certhandler::CertInfo& info) override
    {
        iamanager::v5::CertInfo grpcCertInfo;

        grpcCertInfo.set_type(mCertType);
        grpcCertInfo.set_key_url(info.mKeyURL.CStr());
        grpcCertInfo.set_cert_url(info.mCertURL.CStr());

        WriteToStreams(grpcCertInfo);
    }

    std::string mCertType;
};

} // namespace aos::iam::iamserver

#endif
