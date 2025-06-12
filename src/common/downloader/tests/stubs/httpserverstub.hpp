/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HTTP_SERVER_STUB_HPP_
#define HTTP_SERVER_STUB_HPP_

#include <fstream>
#include <optional>
#include <thread>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/StreamCopier.h>
#include <Poco/Util/ServerApplication.h>

/**
 * File request handler.
 */
class FileRequestHandler : public Poco::Net::HTTPRequestHandler {
public:
    /**
     * Constructor.
     *
     * @param filePath file path.
     */
    explicit FileRequestHandler(const std::string& filePath)
        : mFilePath(filePath)
    {
    }

    /**
     * Handle request.
     *
     * @param request request.
     * @param response response.
     */
    void handleRequest(
        [[maybe_unused]] Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override
    {
        std::ifstream ifs(mFilePath, std::ios::binary);
        if (ifs) {
            response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
            response.setContentType("application/octet-stream");

            Poco::StreamCopier::copyStream(ifs, response.send());
        } else {
            response.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
            response.send() << "File not found";
        }
    }

private:
    std::string mFilePath;
};

/**
 * File request handler factory.
 */
class FileRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
public:
    /**
     * Constructor.
     *
     * @param filePath file path.
     */
    explicit FileRequestHandlerFactory(const std::string& filePath)
        : mFilePath(filePath)
    {
    }

    /**
     * Create request handler.
     *
     * @param request request.
     * @return Poco::Net::HTTPRequestHandler.
     */
    Poco::Net::HTTPRequestHandler* createRequestHandler(
        [[maybe_unused]] const Poco::Net::HTTPServerRequest& request) override
    {
        return new FileRequestHandler(mFilePath);
    }

private:
    std::string mFilePath;
};

/**
 * HTTP server.
 */
class HTTPServer {
public:
    /**
     * Constructor.
     *
     * @param filePath file path.
     * @param port port.
     */
    HTTPServer(const std::string& filePath, int port)
        : mFilePath(filePath)
        , mPort(port)
    {
    }

    /**
     * Start server.
     */
    void Start()
    {
        mServerThread = std::thread([this]() {
            Poco::Net::ServerSocket svs(mPort);

            mServer.emplace(new FileRequestHandlerFactory(mFilePath), Poco::Net::ServerSocket(mPort),
                new Poco::Net::HTTPServerParams);

            mServer->start();
        });
    }

    /**
     * Stop server.
     */
    void Stop()
    {
        mServer->stop();

        if (mServerThread.joinable()) {
            mServerThread.join();
        }
    }

private:
    std::string                          mFilePath;
    int                                  mPort;
    std::thread                          mServerThread;
    std::optional<Poco::Net::HTTPServer> mServer;
};

#endif // HTTP_SERVER_HPP_
