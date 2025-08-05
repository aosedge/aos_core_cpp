/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <thread>

#include <core/common/cloudprotocol/alerts.hpp>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>

#include "downloader.hpp"

namespace aos::common::downloader {

using namespace std::chrono;

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Downloader::Init(aos::alerts::SenderItf* sender, std::chrono::seconds progressInterval)
{
    mSender           = sender;
    mProgressInterval = progressInterval;

    return ErrorEnum::eNone;
}

Downloader::~Downloader()
{
    std::lock_guard<std::mutex> lock {mMutex};

    mShutdown = true;
    mCondVar.notify_all();
}

Error Downloader::Download(const String& url, const String& path, cloudprotocol::DownloadTarget targetType,
    const String& targetID, const String& version)
{
    LOG_DBG() << "Start download" << Log::Field("url", url) << Log::Field("path", path)
              << Log::Field("targetType", targetType) << Log::Field("targetID", targetID)
              << Log::Field("version", version);

    mTargetType = targetType;
    mTargetID   = targetID.CStr();
    mVersion    = version.CStr();
    mURL        = url.CStr();

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

    mExistingOffset = ftell(fp.get());

    if (uri.getScheme() == "http" || uri.getScheme() == "https") {
        curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.CStr());
    curl_easy_setopt(curl.get(), CURLOPT_RESUME_FROM_LARGE, mExistingOffset);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, fp.get());
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, cTimeoutSec); // Timeout in seconds
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, cTimeoutSec);

    if (mSender) {
        mLastProgressTime = steady_clock::now();

        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, &Downloader::XferInfoCallback);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, this);
    }

    if (auto res = curl_easy_perform(curl.get()); res != CURLE_OK) {
        if (res == CURLE_HTTP_RETURNED_ERROR) {
            long code = 0;

            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);

            LOG_ERR() << "HTTP error: " << Log::Field("HTTP_CODE", code);
        }

        std::string msg = curl_easy_strerror(res);

        SendAlert("Download interrupted reason: " + msg);

        return Error(ErrorEnum::eFailed, msg.c_str());
    }

    SendAlert("Download completed");

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

/***********************************************************************************************************************
 * Progress callback
 **********************************************************************************************************************/

int Downloader::XferInfoCallback(
    void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    return static_cast<Downloader*>(clientp)->OnProgress(dltotal, dlnow, ultotal, ulnow);
}

int Downloader::OnProgress(
    curl_off_t dltotal, curl_off_t dlnow, [[maybe_unused]] curl_off_t ultotal, [[maybe_unused]] curl_off_t ulnow)
{
    auto now = steady_clock::now();

    if (now - mLastProgressTime < mProgressInterval) {
        return 0;
    }

    mLastProgressTime = now;

    curl_off_t nowBytes = mExistingOffset + dlnow;

    LOG_DBG() << "Download progress" << Log::Field("complete", nowBytes) << Log::Field("total", dltotal);

    SendAlert("Download status", std::to_string(nowBytes), std::to_string(dltotal));

    return 0;
}

void Downloader::PrepareDownloadAlert(cloudprotocol::DownloadAlert& alert, const std::string& msg,
    const std::string& downloadedBytes, const std::string& totalBytes)
{
    alert.mTargetType      = mTargetType;
    alert.mTargetID        = mTargetID.c_str();
    alert.mVersion         = mVersion.c_str();
    alert.mURL             = mURL.c_str();
    alert.mMessage         = msg.c_str();
    alert.mDownloadedBytes = downloadedBytes.c_str();
    alert.mTotalBytes      = totalBytes.c_str();
}

void Downloader::SendAlert(const std::string& msg, const std::string& downloadedBytes, const std::string& totalBytes)
{
    if (!mSender) {
        return;
    }

    cloudprotocol::DownloadAlert alert;
    cloudprotocol::AlertVariant  param;

    PrepareDownloadAlert(alert, msg, downloadedBytes, totalBytes);
    param.SetValue<cloudprotocol::DownloadAlert>(alert);
    mSender->SendAlert(param);
}

} // namespace aos::common::downloader
