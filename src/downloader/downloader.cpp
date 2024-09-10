/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include <Poco/FileStream.h>
#include <Poco/StreamCopier.h>
#include <Poco/Task.h>
#include <Poco/ThreadPool.h>
#include <curl/curl.h>

#include <utils/exception.hpp>

#include "downloader.hpp"
#include "logger/logmodule.hpp"

namespace aos::mp::downloader {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static std::string GetFileNameFromURL(const std::string& url)
{
    Poco::URI   uri(url);
    std::string path = uri.getPath();

    if (uri.getScheme() == "file") {
        if (path.empty() && !uri.getHost().empty()) {
            path = "/" + uri.getHost();
        }
    }

    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }

    return path;
}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Downloader::Downloader(const std::string& downloadDir)
    : mDownloadDir(downloadDir)
    , mTaskManager(Poco::ThreadPool::defaultPool())
{
    try {
        if (!std::filesystem::exists(mDownloadDir)) {
            std::filesystem::create_directories(mDownloadDir);
        }
    } catch (const std::exception& e) {
        AOS_ERROR_THROW("failed to create download directory: downloadDir=" + mDownloadDir, ErrorEnum::eFailed);
    }
}

RetWithError<std::string> Downloader::Download(const std::string& url)
{
    LOG_DBG() << "Sync downloading: url=" << url.c_str();

    std::string outfilename = std::filesystem::path(mDownloadDir).append(GetFileNameFromURL(url)).string();

    return {outfilename, RetryDownload(url, outfilename)};
}

Downloader::~Downloader()
{
    mTaskManager.cancelAll();
    mTaskManager.joinAll();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error Downloader::Download(const std::string& url, const std::string& outfilename)
{
    Poco::URI uri(url);
    if (uri.getScheme() == "file") {
        return CopyFile(uri, outfilename);
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

    std::unique_ptr<FILE, decltype(fileCloser)> fp(fopen(outfilename.c_str(), "ab"), fileCloser);
    if (!fp) {
        return Error(ErrorEnum::eFailed, "Failed to open file");
    }

    fseek(fp.get(), 0, SEEK_END);

    auto existingFileSize = ftell(fp.get());

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
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

Error Downloader::CopyFile(const Poco::URI& uri, const std::string& outfilename)
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
        std::ofstream ofs(outfilename, std::ios::binary | std::ios::trunc);

        ofs << ifs.rdbuf();
    } catch (const std::exception& e) {
        return Error(ErrorEnum::eFailed, e.what());
    }

    return ErrorEnum::eNone;
}

Error Downloader::RetryDownload(const std::string& url, const std::string& outfilename)
{
    auto  delay = cDelay;
    Error error;

    for (int retryCount = 0; retryCount < cMaxRetryCount; retryCount++) {
        LOG_DBG() << "Downloading: url=" << url.c_str() << ", retry=" << retryCount;

        if (error = Download(url, outfilename); error.IsNone()) {
            return ErrorEnum::eNone;
        }

        LOG_ERR() << "Failed to download: error=" << error.Message() << ", retry=" << retryCount;

        std::this_thread::sleep_for(delay);

        delay = std::min(delay * 2, cMaxDelay);
    }

    return error;
}

} // namespace aos::mp::downloader
