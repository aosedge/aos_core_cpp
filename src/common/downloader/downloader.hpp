/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_DOWNLOADER_DOWNLOADER_HPP_
#define AOS_COMMON_DOWNLOADER_DOWNLOADER_HPP_

#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

#include <Poco/URI.h>
#include <curl/curl.h>

#include <core/common/alerts/itf/sender.hpp>
#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/crypto/itf/crypto.hpp>
#include <core/common/downloader/itf/downloader.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>

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
     * Initializes an HTTPS-only downloader with mutual TLS.
     * Trusted roots are loaded from the PKCS#11 root certificate storage.
     *
     * @param certStorage IAM certificate storage.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @param sender alerts sender.
     * @param progressInterval progress interval.
     * @return Error.
     */
    Error Init(std::string_view certStorage, aos::iamclient::CertProviderItf& certProvider,
        crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
        aos::alerts::SenderItf* sender = nullptr, std::chrono::seconds progressInterval = std::chrono::seconds {30});

    /**
     * Downloads file. Tries the URLs in order, each URL is retried with the downloader retry policy.
     *
     * @param ctx cancel context.
     * @param digest file digest.
     * @param urls file URLs.
     * @param path path to file.
     * @return Error: eCanceled if the download is canceled by the cancel context, eInvalidArgument if urls is empty,
     * eAlreadyExist if a download of the same digest is in progress, otherwise the error of the last attempt.
     */
    Error Download(CancelContext& ctx, const String& digest, const Array<StaticString<cURLLen>>& urls,
        const String& path) override;

private:
    constexpr static std::chrono::milliseconds cDelay {1000};
    constexpr static std::chrono::milliseconds cMaxDelay {5000};
    constexpr static int                       cMaxRetryCount {3};
    constexpr static int                       cTimeoutSec {10};

    struct ProgressContext {
        Downloader*                           mDownloader {};
        CancelContext*                        mCancelContext {};
        std::string                           mDigest;
        std::string                           mURL;
        std::chrono::steady_clock::time_point mLastProgressTime;
        curl_off_t                            mExistingOffset {0};
        curl_off_t                            mTotalSize {0};
        curl_off_t                            mDownloadedSize {0};
    };

    Error           ConfigureTLS(CURL* curl);
    static CURLcode SSLContextCallback(CURL* curl, void* sslContext, void* userData);

    RetWithError<bool> SupportsRangeRequests(const String& url, ProgressContext* context);
    Error              DownloadImage(const String& url, const String& path, ProgressContext* context);
    Error              CopyFile(const Poco::URI& uri, const String& outfilename);
    Error RetryDownload(CancelContext& ctx, const String& url, const String& path, ProgressContext* context);
    void  SendAlert(ProgressContext* context, DownloadState state, size_t downloadedBytes, size_t totalBytes,
         const std::string& reason = "", const Error& error = ErrorEnum::eNone);

    static bool IsCanceled(const ProgressContext* context);
    static int  XferInfoCallback(
         void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    static int CancelXferInfoCallback(
        void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    int OnProgress(
        ProgressContext* context, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

    std::mutex mMutex;

    std::chrono::seconds mProgressInterval {std::chrono::seconds {30}};

    aos::alerts::SenderItf* mSender {nullptr};

    std::string                      mCertStorage;
    aos::iamclient::CertProviderItf* mCertProvider {};
    crypto::CertLoaderItf*           mCertLoader {};
    crypto::x509::ProviderItf*       mCryptoProvider {};

    std::set<std::string> mDigestsInProgress;
};

} // namespace aos::common::downloader

#endif
