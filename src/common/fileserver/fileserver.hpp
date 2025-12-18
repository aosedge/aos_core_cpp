/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_FILESERVER_FILESERVER_HPP_
#define AOS_COMMON_FILESERVER_FILESERVER_HPP_

#include <memory>
#include <string>
#include <thread>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include <core/cm/fileserver/itf/fileserver.hpp>
#include <core/common/tools/error.hpp>

namespace aos::common::fileserver {

/**
 * Fileserver.
 */
class Fileserver : public cm::fileserver::FileServerItf {
public:
    /**
     * Default constructor.
     */
    Fileserver() = default;

    /**
     * Initializes object instance.
     *
     * @param serverURL server URL.
     * @param rootDir root directory.
     * @return Error.
     */
    Error Init(const std::string& serverURL, const std::string& rootDir);

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

private:
    static constexpr auto cDefaultPort = 8080;

    std::string                            mRootDir;
    std::unique_ptr<Poco::Net::HTTPServer> mServer;
    std::string                            mHost;
    uint16_t                               mPort {};
    std::thread                            mThread;
};

} // namespace aos::common::fileserver

#endif // AOS_COMMON_FILESERVER_FILESERVER_HPP_
