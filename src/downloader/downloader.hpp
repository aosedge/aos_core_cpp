/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DOWNLOADER_HPP_
#define DOWNLOADER_HPP_

#include <functional>
#include <string>

#include <Poco/TaskManager.h>
#include <Poco/URI.h>

#include <aos/common/tools/error.hpp>

namespace aos::mp::downloader {
/**
 * Downloader.
 */
class Downloader {
public:
    /**
     * Finished callback.
     *
     * @param url URL.
     * @param error Error.
     */
    using FinishedCallback = std::function<void(const std::string&, Error)>;

    /**
     * Constructor.
     *
     * @param downloadDir download directory.
     */
    Downloader(const std::string& downloadDir);

    /**
     * Destructor.
     */
    ~Downloader();

    /**
     * Downloads file synchronously.
     *
     * @param url URL.
     * @return aos::RetWithError<std::string>.
     */
    RetWithError<std::string> Download(const std::string& url);

private:
    constexpr static std::chrono::milliseconds cDelay {1000};
    constexpr static std::chrono::milliseconds cMaxDelay {5000};
    constexpr static int                       cMaxRetryCount {3};
    constexpr static int                       cTimeoutSec {10};

    Error Download(const std::string& url, const std::string& outfilename);
    Error CopyFile(const Poco::URI& uri, const std::string& outfilename);
    Error RetryDownload(const std::string& url, const std::string& outfilename);

    std::string       mDownloadDir;
    Poco::TaskManager mTaskManager;
};

} // namespace aos::mp::downloader

#endif // DOWNLOADER_HPP_
