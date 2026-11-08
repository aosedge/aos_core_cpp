/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <thread>

#include <unistd.h>

#include <Poco/String.h>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>
#include <core/common/types/alerts.hpp>

#include <common/utils/cryptohelper.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/httpstatus.hpp>

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

Error Downloader::Init(std::string_view certStorage, aos::iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider, aos::alerts::SenderItf* sender,
    std::chrono::seconds progressInterval)
{
    mCertStorage    = certStorage;
    mCertProvider   = &certProvider;
    mCertLoader     = &certLoader;
    mCryptoProvider = &cryptoProvider;

    return Init(sender, progressInterval);
}

Error Downloader::Download(
    CancelContext& ctx, const String& digest, const Array<StaticString<cURLLen>>& urls, const String& path)
{
    LOG_DBG() << "Start download" << Log::Field("digest", digest) << Log::Field("path", path)
              << Log::Field("urls", urls.Size());

    if (urls.IsEmpty()) {
        return Error(ErrorEnum::eInvalidArgument, "no download URLs");
    }

    if (ctx.IsCanceled()) {
        return Error(ErrorEnum::eCanceled, "download canceled");
    }

    ProgressContext context;

    context.mDownloader    = this;
    context.mDigest        = digest.CStr();
    context.mCancelContext = &ctx;

    {
        std::lock_guard lock {mMutex};

        if (!mDigestsInProgress.insert(context.mDigest).second) {
            return Error(ErrorEnum::eAlreadyExist, "download already in progress");
        }
    }

    auto releaseDigest = DeferRelease(&context.mDigest, [this](const std::string* digest) {
        std::lock_guard lock {mMutex};

        mDigestsInProgress.erase(*digest);
    });

    Error err;

    for (const auto& url : urls) {
        context.mURL = url.CStr();

        if (err = RetryDownload(ctx, url, path, &context); err.IsNone() || err.Is(ErrorEnum::eCanceled)) {
            break;
        }

        LOG_ERR() << "Failed to download from URL" << Log::Field("url", url) << Log::Field(err);
    }

    return err;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error Downloader::ConfigureTLS(CURL* curl)
{
    if (!mCertProvider) {
        return ErrorEnum::eNone;
    }

    if (auto result = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    if (auto result = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L); result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    // Disable default CA bundle: trusted roots are loaded from PKCS#11 in SSLContextCallback.
    if (auto result = curl_easy_setopt(curl, CURLOPT_CAINFO, static_cast<const char*>(nullptr)); result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    if (auto result = curl_easy_setopt(curl, CURLOPT_CAPATH, static_cast<const char*>(nullptr)); result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    if (auto result = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https"); result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    if (auto result = curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, &Downloader::SSLContextCallback);
        result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    if (auto result = curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, this); result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    if (auto result = curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L); result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    if (auto result = curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L); result != CURLE_OK) {
        return Error(ErrorEnum::eFailed, curl_easy_strerror(result));
    }

    return ErrorEnum::eNone;
}

CURLcode Downloader::SSLContextCallback(CURL*, void* sslContext, void* userData)
{
    auto* downloader = static_cast<Downloader*>(userData);

    try {
        if (auto err = utils::ConfigureSSLContext(downloader->mCertStorage.c_str(), *downloader->mCertProvider,
                *downloader->mCertLoader, *downloader->mCryptoProvider, static_cast<SSL_CTX*>(sslContext));
            !err.IsNone()) {
            LOG_ERR() << "Failed to configure downloader TLS" << Log::Field(err);

            return CURLE_SSL_CERTPROBLEM;
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to configure downloader TLS" << utils::ToAosError(e);

        return CURLE_SSL_CERTPROBLEM;
    } catch (...) {
        return CURLE_SSL_CERTPROBLEM;
    }

    return CURLE_OK;
}

Error Downloader::DownloadImage(const String& url, const String& path, ProgressContext* context)
{
    Poco::URI uri;

    try {
        uri = Poco::URI(url.CStr());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    if (uri.getScheme() == "file") {
        return CopyFile(uri, path);
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) {
        return Error(ErrorEnum::eFailed, "failed to init curl");
    }

    if (auto err = ConfigureTLS(curl.get()); !err.IsNone()) {
        return err;
    }

    curl_off_t existingSize = 0;

    try {
        if (std::filesystem::exists(path.CStr())) {
            existingSize = std::filesystem::file_size(path.CStr());
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    auto fileCloser = [](FILE* fp) {
        if (fp) {
            if (auto res = fclose(fp); res != 0) {
                LOG_ERR() << "failed to close file: res=" << res;
            }
        }
    };

    // Open in append mode when resuming: if the server ignores the range, the file is truncated before the first
    // write (see CheckRangeResponse), and append mode makes the writes continue from the new end of the file.
    std::unique_ptr<FILE, decltype(fileCloser)> fp(fopen(path.CStr(), existingSize > 0 ? "ab" : "wb"), fileCloser);
    if (!fp) {
        return Error(ErrorEnum::eFailed, "failed to open file");
    }

    context->mExistingOffset = existingSize;

    if (uri.getScheme() == "http" || uri.getScheme() == "https") {
        curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.CStr());

    // Resume with a ranged GET instead of probing range support with a HEAD request: presigned URLs (e.g. S3) are
    // signed for a single HTTP method, so a HEAD request to a URL signed for GET is rejected with 403.
    if (existingSize > 0) {
        LOG_DBG() << "Resume download" << Log::Field("offset", static_cast<uint64_t>(existingSize));

        // libcurl copies the range string, so it doesn't need to outlive this scope.
        const auto range = std::to_string(existingSize) + "-";

        curl_easy_setopt(curl.get(), CURLOPT_RANGE, range.c_str());
    }

    WriteContext writeContext {curl.get(), fp.get(), context};

    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &Downloader::WriteCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &writeContext);
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

            if (code == cRangeNotSatisfiableHTTPStatus && existingSize > 0) {
                // The partial file doesn't match the remote one (e.g. it is already complete or bigger): drop it
                // and download the whole file.
                LOG_WRN() << "Range not satisfiable, restart download from beginning";

                fp.reset();

                try {
                    std::filesystem::remove(path.CStr());
                } catch (const std::exception& e) {
                    return AOS_ERROR_WRAP(utils::ToAosError(e));
                }

                return DownloadImage(url, path, context);
            }

            if (code == utils::cIntegrityHTTPStatus) {
                err = Error(ErrorEnum::eIntegrity, "blob integrity error");
            } else {
                err = Error(code, curl_easy_strerror(res));
            }
        } else {
            err = Error(ErrorEnum::eFailed, curl_easy_strerror(res));
        }

        SendAlert(context, DownloadStateEnum::eInterrupted, context->mDownloadedSize, context->mTotalSize,
            curl_easy_strerror(res), err);

        return err;
    }

    // Response without body: the write callback was not called, check the range response here.
    if (!CheckRangeResponse(&writeContext)) {
        return Error(ErrorEnum::eFailed, "failed to truncate file");
    }

    SendAlert(context, DownloadStateEnum::eFinished, context->mDownloadedSize, context->mTotalSize);

    return ErrorEnum::eNone;
}

bool Downloader::CheckRangeResponse(WriteContext* writeContext)
{
    if (writeContext->mRangeChecked) {
        return true;
    }

    writeContext->mRangeChecked = true;

    if (writeContext->mProgressContext->mExistingOffset == 0) {
        return true;
    }

    long code = 0;

    curl_easy_getinfo(writeContext->mCurl, CURLINFO_RESPONSE_CODE, &code);

    if (code == cPartialContentHTTPStatus) {
        return true;
    }

    // The server ignored the range and sends the whole file: start from the beginning.
    LOG_DBG() << "Server does not support range requests, starting from beginning" << Log::Field("HTTP_CODE", code);

    if (fflush(writeContext->mFile) != 0 || ftruncate(fileno(writeContext->mFile), 0) != 0) {
        LOG_ERR() << "Failed to truncate file" << Log::Field(Error(errno));

        return false;
    }

    writeContext->mProgressContext->mExistingOffset = 0;

    return true;
}

// cppcheck-suppress constParameterCallback
size_t Downloader::WriteCallback(char* data, size_t size, size_t nmemb, void* userData)
{
    auto* writeContext = static_cast<WriteContext*>(userData);

    if (!CheckRangeResponse(writeContext)) {
        // Returning a size different from the passed one aborts the transfer.
        return 0;
    }

    return fwrite(data, size, nmemb, writeContext->mFile);
}

Error Downloader::CopyFile(const Poco::URI& uri, const String& outfilename)
{
    auto path = uri.getPath();
    if (path.empty() && !uri.getHost().empty()) {
        path = uri.getHost();
    }

    try {
        if (!std::filesystem::exists(path)) {
            return Error(ErrorEnum::eFailed, "file not found");
        }

        std::filesystem::copy_file(path, outfilename.CStr(), std::filesystem::copy_options::overwrite_existing);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }
}

Error Downloader::RetryDownload(CancelContext& ctx, const String& url, const String& path, ProgressContext* context)
{
    if (mCertProvider) {
        try {
            if (Poco::URI(url.CStr()).getScheme() != "https") {
                return Error(ErrorEnum::eInvalidArgument, "mutual TLS download requires HTTPS");
            }
        } catch (const std::exception& e) {
            return AOS_ERROR_WRAP(utils::ToAosError(e));
        }
    }

    auto  delay = cDelay;
    Error err;

    for (int retryCount = 0; retryCount < cMaxRetryCount; ++retryCount) {
        if (IsCanceled(context)) {
            return Error(ErrorEnum::eCanceled, "download canceled");
        }

        LOG_DBG() << "Downloading" << Log::Field("url", url) << Log::Field("retry", retryCount);

        if (err = DownloadImage(url, path, context); err.IsNone()) {
            LOG_DBG() << "Download success" << Log::Field("url", url);

            return ErrorEnum::eNone;
        }

        if (IsCanceled(context)) {
            return Error(ErrorEnum::eCanceled, "download canceled");
        }

        if (err.Is(ErrorEnum::eIntegrity)) {
            return err;
        }

        LOG_ERR() << "Failed to download" << Log::Field("retry", retryCount) << Log::Field(AOS_ERROR_WRAP(err));

        if (retryCount + 1 == cMaxRetryCount) {
            break;
        }

        if (auto waitErr = ctx.Wait(delay.count() * Time::cMilliseconds); !waitErr.Is(ErrorEnum::eTimeout)) {
            return AOS_ERROR_WRAP(waitErr);
        }

        delay = std::min(delay * 2, cMaxDelay);
    }

    return err;
}

/***********************************************************************************************************************
 * Progress callback
 **********************************************************************************************************************/

bool Downloader::IsCanceled(const ProgressContext* context)
{
    return context->mCancelContext && context->mCancelContext->IsCanceled();
}

int Downloader::XferInfoCallback(
    void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    auto* ctx = static_cast<ProgressContext*>(clientp);

    if (IsCanceled(ctx)) {
        LOG_DBG() << "Download canceled via progress callback";

        return 1;
    }

    return ctx->mDownloader->OnProgress(ctx, dltotal, dlnow, ultotal, ulnow);
}

int Downloader::OnProgress(ProgressContext* context, curl_off_t dltotal, curl_off_t dlnow,
    [[maybe_unused]] curl_off_t ultotal, [[maybe_unused]] curl_off_t ulnow)
{
    context->mDownloadedSize = context->mExistingOffset + dlnow;
    context->mTotalSize      = dltotal;

    if (!mSender) {
        return 0;
    }

    auto now = steady_clock::now();

    if (now - context->mLastProgressTime < mProgressInterval) {
        return 0;
    }

    context->mLastProgressTime = now;

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

    auto alert = std::make_unique<DownloadAlert>();

    alert->mDigest          = context->mDigest.c_str();
    alert->mURL             = context->mURL.c_str();
    alert->mState           = state;
    alert->mDownloadedBytes = downloadedBytes;
    alert->mTotalBytes      = totalBytes;

    if (!reason.empty()) {
        alert->mReason.SetValue(reason.c_str());
    }

    alert->mError = error;

    AlertVariant param;

    param.SetValue<DownloadAlert>(*alert);

    mSender->SendAlert(param);
}

} // namespace aos::common::downloader
