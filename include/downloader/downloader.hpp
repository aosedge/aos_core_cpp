/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DOWNLOADER_HPP_
#define DOWNLOADER_HPP_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

#include <Poco/URI.h>

#include <aos/common/downloader/downloader.hpp>

namespace aos::common::downloader {

/**
 * Downloader.
 */
class Downloader : public aos::downloader::DownloaderItf {
public:
    /**
     * Destructor.
     */
    ~Downloader();

    /**
     * Downloads file.
     *
     * @param url URL.
     * @param path path to file.
     * @param contentType content type.
     * @return Error.
     */
    Error Download(const String& url, const String& path, aos::downloader::DownloadContent contentType) override;

private:
    constexpr static std::chrono::milliseconds cDelay {1000};
    constexpr static std::chrono::milliseconds cMaxDelay {5000};
    constexpr static int                       cMaxRetryCount {3};
    constexpr static int                       cTimeoutSec {10};

    Error Download(const String& url, const String& path);
    Error CopyFile(const Poco::URI& uri, const String& outfilename);
    Error RetryDownload(const String& url, const String& path);

    bool                    mShutdown {false};
    std::mutex              mMutex;
    std::condition_variable mCondVar;
};

} // namespace aos::common::downloader

#endif
