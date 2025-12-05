/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_DOWNLOADER_DOWNLOADER_HPP_
#define AOS_COMMON_DOWNLOADER_DOWNLOADER_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>

#include <Poco/URI.h>
#include <curl/curl.h>

#include <core/common/alerts/itf/sender.hpp>
#include <core/common/downloader/itf/downloader.hpp>

namespace aos::common::downloader {

/**
 * Downloader.
 */
class Downloader : public aos::downloader::DownloaderItf {
public:
    /**
     * Initializes object instance.
     *
     * @param sender alerts sender.
     * @param progressInterval progress interval.
     * @return Error.
     */
    Error Init(
        aos::alerts::SenderItf* sender = nullptr, std::chrono::seconds progressInterval = std::chrono::seconds {30});

    /**
     * Destructor.
     */
    ~Downloader();

    /**
     * Downloads file.
     *
     * @param digest image digest.
     * @param url URL.
     * @param path path to file.
     * @return Error.
     */
    Error Download(const String& digest, const String& url, const String& path) override;

    /**
     * Cancels ongoing download.
     *
     * @param digest image digest.
     *
     * @return Error.
     */
    Error Cancel(const String& digest) override;

private:
    constexpr static std::chrono::milliseconds cDelay {1000};
    constexpr static std::chrono::milliseconds cMaxDelay {5000};
    constexpr static int                       cMaxRetryCount {3};
    constexpr static int                       cTimeoutSec {10};

    struct ProgressContext {
        Downloader*                           mDownloader {};
        std::atomic<bool>*                    mCancelFlag {};
        std::string                           mDigest;
        std::string                           mURL;
        std::chrono::steady_clock::time_point mLastProgressTime;
        curl_off_t                            mExistingOffset {0};
        curl_off_t                            mTotalSize {0};
        curl_off_t                            mDownloadedSize {0};
    };

    Error DownloadImage(const String& url, const String& path, ProgressContext* context);
    Error CopyFile(const Poco::URI& uri, const String& outfilename);
    Error RetryDownload(const String& url, const String& path, ProgressContext* context);
    void  SendAlert(ProgressContext* context, DownloadState state, size_t downloadedBytes, size_t totalBytes,
         const std::string& reason = "", const Error& error = ErrorEnum::eNone);

    static int XferInfoCallback(
        void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    int OnProgress(
        ProgressContext* context, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

    bool                    mShutdown {false};
    std::mutex              mMutex;
    std::condition_variable mCondVar;

    std::chrono::seconds mProgressInterval {std::chrono::seconds {30}};

    aos::alerts::SenderItf* mSender {nullptr};

    std::unordered_map<std::string, std::atomic<bool>> mCancelFlags;
};

} // namespace aos::common::downloader

#endif
