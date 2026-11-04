/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_FILESERVER_FILESERVER_HPP_
#define AOS_COMMON_FILESERVER_FILESERVER_HPP_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <Poco/Net/Context.h>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/URI.h>

#include <core/cm/fileserver/itf/fileserver.hpp>
#include <core/common/tools/error.hpp>

#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/crypto/itf/crypto.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>

namespace aos::common::fileserver {

/**
 * In-memory one-shot registry of files that failed the integrity check.
 *
 * A read error that happens after the response headers are sent can't be reported in the response status. Such a file
 * is marked, and the next request of the same unchanged file is answered with the integrity status without reading the
 * file. The mark is dropped by that request, so the following request reads the file again; a file that is still
 * broken is marked again by the later request. A mark of a file with another modification time is dropped as stale.
 */
class BrokenFiles {
public:
    /**
     * Marks file as broken. The mark is bound to the file modification time.
     *
     * @param path file path.
     * @param mtime modification time of the file that failed to be read.
     */
    void Mark(const std::string& path, const struct timespec& mtime);

    /**
     * Takes file mark: removes the mark of the file if any.
     *
     * @param path file path.
     * @param st current file status.
     * @return true if file was marked with the same modification time.
     */
    bool Take(const std::string& path, const struct stat& st);

private:
    std::mutex                                       mMutex;
    std::unordered_map<std::string, struct timespec> mFiles;
};

/**
 * Fileserver.
 */
class Fileserver : public cm::fileserver::FileServerItf, public aos::iamclient::CertKeyListenerItf {
public:
    using SSLContextConfigurator = std::function<Error(SSL_CTX*)>;
    using ReadFunction           = std::function<ssize_t(int fd, void* buf, size_t count)>;

    /**
     * Constructs a fileserver with an optional TLS configurator and retry interval.
     * Empty configurator selects the IAM-backed implementation.
     */
    explicit Fileserver(SSLContextConfigurator configureContext = {},
        std::chrono::milliseconds              retryInterval    = std::chrono::seconds(10));

    /**
     * Stops the server and certificate subscription.
     */
    ~Fileserver();

    /**
     * Initializes object instance.
     *
     * @param serverURL server URL.
     * @param rootDir root directory.
     * @param certStorage IAM certificate storage.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @return Error.
     */
    Error Init(const std::string& serverURL, const std::string& rootDir, std::string_view certStorage,
        aos::iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
        crypto::x509::ProviderItf& cryptoProvider);

    /**
     * Translates file path URL.
     *
     * @param filePath input file path.
     * @param[out] outURL translated URL.
     * @return Error.
     */
    Error TranslateFilePathURL(const String& filePath, String& outURL) override;

    /**
     * File request handler factory.
     */
    class FileRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
    public:
        /**
         * Constructor.
         *
         * @param rootDir root directory.
         * @param read file read function.
         */
        explicit FileRequestHandlerFactory(const std::string& rootDir, ReadFunction read = ::read);

        /**
         * Create request handler.
         *
         * @param request request.
         * @return request handler.
         */
        Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest& request) override;

    private:
        std::string                  mRootDir;
        ReadFunction                 mRead;
        std::shared_ptr<BrokenFiles> mBrokenFiles;
    };

    /**
     * File request handler.
     */
    class FileRequestHandler : public Poco::Net::HTTPRequestHandler {
    public:
        /**
         * Constructor.
         *
         * @param rootDir root directory.
         * @param brokenFiles broken files registry.
         * @param read file read function.
         */
        FileRequestHandler(const std::string& rootDir, std::shared_ptr<BrokenFiles> brokenFiles, ReadFunction read);

        /**
         * Handle request.
         *
         * @param request request.
         * @param response response.
         */
        void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override;

    private:
        /**
         * File descriptor handle used as unique pointer pointer type.
         */
        struct FDHandle {
            int mFD = -1;

            FDHandle(std::nullptr_t = nullptr) { }
            FDHandle(int fd)
                : mFD(fd)
            {
            }

            explicit operator bool() const { return mFD >= 0; }

            friend bool operator==(FDHandle lhs, FDHandle rhs) { return lhs.mFD == rhs.mFD; }
            friend bool operator!=(FDHandle lhs, FDHandle rhs) { return !(lhs == rhs); }
        };

        /**
         * File descriptor closer.
         */
        struct FDCloser {
            using pointer = FDHandle;

            void operator()(FDHandle handle) const { close(handle.mFD); }
        };

        using FDPtr = std::unique_ptr<int, FDCloser>;

        /**
         * Resolves request target to a file path inside the root directory.
         *
         * @param target request target without query.
         * @param[out] path resolved file path.
         * @return true if the resolved path is inside the root directory.
         */
        bool ResolvePath(const std::string& target, std::filesystem::path& path) const;

        /**
         * Opens the file and checks that it can be served. Sends the error response otherwise.
         *
         * @param path file path.
         * @param[out] st file status.
         * @param response response.
         * @return opened file descriptor or null if a response was sent.
         */
        FDPtr OpenFile(const std::string& path, struct stat& st, Poco::Net::HTTPServerResponse& response);

        /**
         * Handles file read error: logs it, sends the error response if the response headers are not sent yet,
         * otherwise marks the file on integrity error.
         *
         * @param path file path.
         * @param st file status.
         * @param err errno value.
         * @param response response.
         */
        void HandleReadError(
            const std::string& path, const struct stat& st, int err, Poco::Net::HTTPServerResponse& response);

        /**
         * Sends the file content.
         *
         * @param fd opened file descriptor.
         * @param path file path.
         * @param st file status.
         * @param extension file extension.
         * @param request request.
         * @param response response.
         */
        void SendFile(int fd, const std::string& path, const struct stat& st, const std::string& extension,
            Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);

        std::string                  mRootDir;
        std::shared_ptr<BrokenFiles> mBrokenFiles;
        ReadFunction                 mRead;
    };

    /**
     * Starts server.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops server.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Schedules certificate reload outside the IAM notification thread.
     *
     * @param info updated certificate info.
     */
    void OnCertKeyChanged(const CertInfo& info) override;

private:
    Error ReloadCertificate();
    Error CreateSSLContext(Poco::Net::Context::Ptr& context);
    Error StartServer(const Poco::Net::Context::Ptr& context);
    Error StopServer();
    void  ProcessCertificateChanges();

    static constexpr auto cDefaultPort = 8080;

    SSLContextConfigurator    mConfigureContext;
    std::chrono::milliseconds mRetryInterval;
    std::mutex                mMutex;
    std::condition_variable   mCondVar;
    std::thread               mCertUpdateThread;
    bool                      mCertChanged {false};
    bool                      mShutdown {true};

    std::string                            mRootDir;
    std::unique_ptr<Poco::Net::HTTPServer> mServer;
    Poco::URI                              mURI;
    std::string                            mCertStorage;
    aos::iamclient::CertProviderItf*       mCertProvider {};
    crypto::CertLoaderItf*                 mCertLoader {};
    crypto::x509::ProviderItf*             mCryptoProvider {};
};

} // namespace aos::common::fileserver

#endif // AOS_COMMON_FILESERVER_FILESERVER_HPP_
