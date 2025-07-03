/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_DOWNLOADER_HTTPSERVERSTUB_HPP_
#define AOS_COMMON_DOWNLOADER_HTTPSERVERSTUB_HPP_

#include <chrono>
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
     * @param delayMs delay in milliseconds between chunks.
     */
    explicit FileRequestHandler(const std::string& filePath, int delayMs = 0)
        : mFilePath(filePath)
        , mDelayMs(delayMs)
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

            if (mDelayMs > 0) {
                const size_t chunkSize = 64 * 1024;
                char         buffer[chunkSize];
                auto&        output = response.send();

                while (ifs.read(buffer, chunkSize) || ifs.gcount() > 0) {
                    std::streamsize bytesRead = ifs.gcount();
                    output.write(buffer, bytesRead);
                    output.flush();

                    std::this_thread::sleep_for(std::chrono::milliseconds(mDelayMs));
                }
            } else {
                Poco::StreamCopier::copyStream(ifs, response.send());
            }
        } else {
            response.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
            response.send() << "File not found";
        }
    }

private:
    std::string mFilePath;
    int         mDelayMs;
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
     * @param delayMs delay in milliseconds between chunks.
     */
    explicit FileRequestHandlerFactory(const std::string& filePath, int delayMs = 0)
        : mFilePath(filePath)
        , mDelayMs(delayMs)
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
        return new FileRequestHandler(mFilePath, mDelayMs);
    }

private:
    std::string mFilePath;
    int         mDelayMs;
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
     * @param delayMs delay in milliseconds between chunks.
     */
    HTTPServer(const std::string& filePath, int port, int delayMs = 0)
        : mFilePath(filePath)
        , mPort(port)
        , mDelayMs(delayMs)
    {
    }

    /**
     * Start server.
     */
    void Start()
    {
        mServerThread = std::thread([this]() {
            Poco::Net::ServerSocket svs(mPort);

            mServer.emplace(new FileRequestHandlerFactory(mFilePath, mDelayMs), Poco::Net::ServerSocket(mPort),
                new Poco::Net::HTTPServerParams);

            mServer->start();
        });
    }

    /**
     * Stop server.
     */
    void Stop()
    {
        if (mServer) {
            mServer->stop();
        }

        if (mServerThread.joinable()) {
            mServerThread.join();
        }
    }

private:
    std::string                          mFilePath;
    int                                  mPort;
    int                                  mDelayMs;
    std::thread                          mServerThread;
    std::optional<Poco::Net::HTTPServer> mServer;
};

#endif
