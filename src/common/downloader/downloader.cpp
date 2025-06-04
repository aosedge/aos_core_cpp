/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <thread>

#include <curl/curl.h>

#include "downloader/downloader.hpp"
#include "logger/logmodule.hpp"
#include "utils/exception.hpp"

namespace aos::common::downloader {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Downloader::~Downloader()
{
    std::lock_guard<std::mutex> lock {mMutex};

    mShutdown = true;
    mCondVar.notify_all();
}

Error Downloader::Download(const String& url, const String& path, aos::downloader::DownloadContent contentType)
{
    LOG_DBG() << "Start download: url=" << url << ", path=" << path << ", contentType=" << contentType;

    return RetryDownload(url, path);
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error Downloader::Download(const String& url, const String& path)
{
    Poco::URI uri(url.CStr());
    if (uri.getScheme() == "file") {
        return CopyFile(uri, path);
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) {
        return Error(ErrorEnum::eFailed, "Failed to init curl");
    }

    auto fileCloser = [](FILE* fp) {
        if (fp) {
            if (auto res = fclose(fp); res != 0) {
                LOG_ERR() << "Failed to close file: res=" << res;
            }
        }
    };

    std::unique_ptr<FILE, decltype(fileCloser)> fp(fopen(path.CStr(), "ab"), fileCloser);
    if (!fp) {
        return Error(ErrorEnum::eFailed, "Failed to open file");
    }

    fseek(fp.get(), 0, SEEK_END);

    auto existingFileSize = ftell(fp.get());

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.CStr());
    curl_easy_setopt(curl.get(), CURLOPT_RESUME_FROM_LARGE, existingFileSize);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, fp.get());
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, cTimeoutSec); // Timeout in seconds
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, cTimeoutSec);

    auto res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(res));
    }

    return ErrorEnum::eNone;
}

Error Downloader::CopyFile(const Poco::URI& uri, const String& outfilename)
{
    auto path = uri.getPath();
    if (path.empty() && !uri.getHost().empty()) {
        path = uri.getHost();
    }

    if (!std::filesystem::exists(path)) {
        return Error(ErrorEnum::eFailed, "File not found");
    }

    try {
        std::ifstream ifs(path, std::ios::binary);
        std::ofstream ofs(outfilename.CStr(), std::ios::binary | std::ios::trunc);

        ofs << ifs.rdbuf();

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }
}

Error Downloader::RetryDownload(const String& url, const String& path)
{
    auto  delay = cDelay;
    Error err;

    for (int retryCount = 0; (retryCount < cMaxRetryCount) && (!mShutdown); ++retryCount) {
        LOG_DBG() << "Downloading: url=" << url << ", retry=" << retryCount;

        if (err = Download(url, path); err.IsNone()) {
            LOG_DBG() << "Download success: url=" << url;

            return ErrorEnum::eNone;
        }

        LOG_ERR() << "Failed to download: err=" << err.Message() << ", retry=" << retryCount;

        {
            std::unique_lock<std::mutex> lock {mMutex};

            mCondVar.wait_for(lock, delay, [this] { return mShutdown; });
        }

        delay = std::min(delay * 2, cMaxDelay);
    }

    return err;
}

} // namespace aos::common::downloader
