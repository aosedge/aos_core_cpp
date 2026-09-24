/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_FILESERVER_FILESERVER_HPP_
#define AOS_COMMON_FILESERVER_FILESERVER_HPP_

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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
 * Fileserver.
 */
class Fileserver : public cm::fileserver::FileServerItf, public aos::iamclient::CertListenerItf {
public:
    using SSLContextConfigurator = std::function<Error(SSL_CTX*)>;

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
     * @param caCert CA certificate path.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @return Error.
     */
    Error Init(const std::string& serverURL, const std::string& rootDir, const std::string& certStorage,
        const std::string& caCert, aos::iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
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
         */
        explicit FileRequestHandlerFactory(const std::string& rootDir);

        /**
         * Create request handler.
         *
         * @param request request.
         * @return request handler.
         */
        Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest& request) override;

    private:
        std::string mRootDir;
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
         */
        explicit FileRequestHandler(const std::string& rootDir);

        /**
         * Handle request.
         *
         * @param request request.
         * @param response response.
         */
        void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override;

    private:
        std::string mRootDir;
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
    void OnCertChanged(const CertInfo& info) override;

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
    std::string                            mCACert;
    aos::iamclient::CertProviderItf*       mCertProvider {};
    crypto::CertLoaderItf*                 mCertLoader {};
    crypto::x509::ProviderItf*             mCryptoProvider {};
};

} // namespace aos::common::fileserver

#endif // AOS_COMMON_FILESERVER_FILESERVER_HPP_
