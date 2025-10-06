/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_DOWNLOADER_DOWNLOADER_HPP_
#define AOS_COMMON_DOWNLOADER_DOWNLOADER_HPP_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

#include <Poco/URI.h>
#include <curl/curl.h>

#include <core/common/alerts/alerts.hpp>
#include <core/common/downloader/downloader.hpp>

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
     * @param url URL.
     * @param path path to file.
     * @param imageID image ID.
     * @return Error.
     */
    Error Download(const String& url, const String& path, const String& imageID = "") override;

private:
    constexpr static std::chrono::milliseconds cDelay {1000};
    constexpr static std::chrono::milliseconds cMaxDelay {5000};
    constexpr static int                       cMaxRetryCount {3};
    constexpr static int                       cTimeoutSec {10};

    Error DownloadImage(const String& url, const String& path);
    Error CopyFile(const Poco::URI& uri, const String& outfilename);
    Error RetryDownload(const String& url, const String& path);
    void  SendAlert(DownloadState state, size_t downloadedBytes, size_t totalBytes, const std::string& reason = "",
         Error error = ErrorEnum::eNone);

    static int XferInfoCallback(
        void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    int OnProgress(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

    bool                    mShutdown {false};
    std::mutex              mMutex;
    std::condition_variable mCondVar;

    std::chrono::steady_clock::time_point mLastProgressTime;
    std::chrono::seconds                  mProgressInterval {std::chrono::seconds {30}};
    curl_off_t                            mExistingOffset {0};
    curl_off_t                            mTotalSize {0};
    curl_off_t                            mDownloadedSize {0};
    std::string                           mImageID;
    std::string                           mURL;

    aos::alerts::SenderItf* mSender {nullptr};
};

} // namespace aos::common::downloader

#endif
