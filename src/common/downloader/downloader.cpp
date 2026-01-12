/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <thread>

#include <core/common/tools/logger.hpp>
#include <core/common/types/alerts.hpp>

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
    std::lock_guard lock {mMutex};

    mShutdown = true;
    mCondVar.notify_all();
}

Error Downloader::Download(const String& digest, const String& url, const String& path)
{
    LOG_DBG() << "Start download" << Log::Field("url", url) << Log::Field("path", path) << Log::Field("digest", digest);

    ProgressContext context;

    context.mDownloader = this;
    context.mDigest     = digest.CStr();
    context.mURL        = url.CStr();

    {
        std::lock_guard lock {mMutex};

        auto it = mCancelFlags.find(digest.CStr());
        if (it != mCancelFlags.end()) {
            return Error(ErrorEnum::eAlreadyExist, "download already in progress");
        }

        mCancelFlags[context.mDigest].store(false);
        context.mCancelFlag = &mCancelFlags[context.mDigest];
    }

    auto err = RetryDownload(url, path, &context);

    {
        std::lock_guard lock {mMutex};
        mCancelFlags.erase(context.mDigest);
    }

    return err;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error Downloader::DownloadImage(const String& url, const String& path, ProgressContext* context)
{
    Poco::URI uri(url.CStr());
    if (uri.getScheme() == "file") {
        return CopyFile(uri, path);
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) {
        return Error(ErrorEnum::eFailed, "failed to init curl");
    }

    auto fileCloser = [](FILE* fp) {
        if (fp) {
            if (auto res = fclose(fp); res != 0) {
                LOG_ERR() << "failed to close file: res=" << res;
            }
        }
    };

    std::unique_ptr<FILE, decltype(fileCloser)> fp(fopen(path.CStr(), "ab"), fileCloser);
    if (!fp) {
        return Error(ErrorEnum::eFailed, "failed to open file");
    }

    fseek(fp.get(), 0, SEEK_END);

    context->mExistingOffset = ftell(fp.get());

    if (uri.getScheme() == "http" || uri.getScheme() == "https") {
        curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.CStr());
    curl_easy_setopt(curl.get(), CURLOPT_RESUME_FROM_LARGE, context->mExistingOffset);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, fp.get());
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, cTimeoutSec); // Timeout in seconds
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, cTimeoutSec);

    context->mLastProgressTime = steady_clock::now();

    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, &Downloader::XferInfoCallback);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, context);

    if (auto res = curl_easy_perform(curl.get()); res != CURLE_OK) {
        Error err;

        if (res == CURLE_HTTP_RETURNED_ERROR) {
            long code = 0;

            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);

            LOG_ERR() << "HTTP error: " << Log::Field("HTTP_CODE", code);

            err = Error(code, curl_easy_strerror(res));
        } else {
            err = Error(ErrorEnum::eFailed, curl_easy_strerror(res));
        }

        SendAlert(context, DownloadStateEnum::eInterrupted, context->mDownloadedSize, context->mTotalSize,
            curl_easy_strerror(res), err);

        return err;
    }

    SendAlert(context, DownloadStateEnum::eFinished, context->mDownloadedSize, context->mTotalSize);

    return ErrorEnum::eNone;
}

Error Downloader::CopyFile(const Poco::URI& uri, const String& outfilename)
{
    auto path = uri.getPath();
    if (path.empty() && !uri.getHost().empty()) {
        path = uri.getHost();
    }

    if (!std::filesystem::exists(path)) {
        return Error(ErrorEnum::eFailed, "file not found");
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

Error Downloader::RetryDownload(const String& url, const String& path, ProgressContext* context)
{
    auto  delay = cDelay;
    Error err;

    for (int retryCount = 0; (retryCount < cMaxRetryCount) && (!mShutdown); ++retryCount) {
        if (context->mCancelFlag && context->mCancelFlag->load()) {
            return Error(ErrorEnum::eRuntime, "download cancelled");
        }

        LOG_DBG() << "Downloading:" << Log::Field("url", url) << Log::Field("retry", retryCount);

        if (err = DownloadImage(url, path, context); err.IsNone()) {
            LOG_DBG() << "Download success" << Log::Field("url", url);

            return ErrorEnum::eNone;
        }

        LOG_ERR() << "Failed to download" << Log::Field("retry", retryCount) << Log::Field(AOS_ERROR_WRAP(err));

        {
            std::unique_lock<std::mutex> lock {mMutex};

            mCondVar.wait_for(lock, delay,
                [this, context] { return mShutdown || (context->mCancelFlag && context->mCancelFlag->load()); });
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
    auto* ctx = static_cast<ProgressContext*>(clientp);

    if (ctx->mCancelFlag && ctx->mCancelFlag->load()) {
        LOG_DBG() << "Download cancelled via progress callback";

        return 1;
    }

    return ctx->mDownloader->OnProgress(ctx, dltotal, dlnow, ultotal, ulnow);
}

int Downloader::OnProgress(ProgressContext* context, curl_off_t dltotal, curl_off_t dlnow,
    [[maybe_unused]] curl_off_t ultotal, [[maybe_unused]] curl_off_t ulnow)
{
    if (!mSender) {
        return 0;
    }

    auto now = steady_clock::now();

    if (now - context->mLastProgressTime < mProgressInterval) {
        return 0;
    }

    context->mLastProgressTime = now;
    context->mDownloadedSize   = context->mExistingOffset + dlnow;
    context->mTotalSize        = dltotal;

    LOG_DBG() << "Download progress" << Log::Field("downloaded", context->mDownloadedSize)
              << Log::Field("total", context->mTotalSize);

    SendAlert(context, DownloadStateEnum::eStarted, context->mDownloadedSize, context->mTotalSize);

    return 0;
}

void Downloader::SendAlert(ProgressContext* context, DownloadState state, size_t downloadedBytes, size_t totalBytes,
    const std::string& reason, const Error& error)
{
    if (!mSender) {
        return;
    }

    DownloadAlert alert;

    alert.mDigest          = context->mDigest.c_str();
    alert.mURL             = context->mURL.c_str();
    alert.mState           = state;
    alert.mDownloadedBytes = downloadedBytes;
    alert.mTotalBytes      = totalBytes;

    if (!reason.empty()) {
        alert.mReason.SetValue(reason.c_str());
    }

    alert.mError = error;

    AlertVariant param;

    param.SetValue<DownloadAlert>(alert);

    mSender->SendAlert(param);
}

Error Downloader::Cancel(const String& digest)
{
    std::lock_guard lock {mMutex};

    if (auto it = mCancelFlags.find(digest.CStr()); it != mCancelFlags.end()) {
        it->second.store(true);

        LOG_DBG() << "Cancel requested for download:" << Log::Field("digest", digest);

        return ErrorEnum::eNone;
    }

    return Error(ErrorEnum::eNotFound, "download not found");
}

} // namespace aos::common::downloader
