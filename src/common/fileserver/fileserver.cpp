/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <chrono>
#include <filesystem>
#include <map>

#include <Poco/DateTimeFormat.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/File.h>
#include <Poco/FileStream.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Path.h>
#include <Poco/StreamCopier.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>

#include "fileserver.hpp"

namespace fs = std::filesystem;

namespace aos::common::fileserver {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

const std::map<std::string, std::string> sMimeTypes = {{"html", "text/html"}, {"htm", "text/html"}, {"css", "text/css"},
    {"js", "application/javascript"}, {"json", "application/json"}, {"xml", "application/xml"}, {"txt", "text/plain"},
    {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"png", "image/png"}, {"gif", "image/gif"}, {"svg", "image/svg+xml"},
    {"ico", "image/x-icon"}, {"pdf", "application/pdf"}, {".zip", "application/zip"}, {".tar", "application/x-tar"},
    {".gz", "application/gzip"}};

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::string GetMimeType(const std::string& ext)
{
    auto it = sMimeTypes.find(ext);
    if (it != sMimeTypes.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Fileserver::Init(const std::string& serverURL, const std::string& rootDir)
{
    try {
        LOG_DBG() << "Init fileserver" << Log::Field("serverURL", serverURL.c_str())
                  << Log::Field("rootDir", rootDir.c_str());

        mRootDir = rootDir;
        mURI     = serverURL.c_str();

        if (mURI.getScheme().empty()) {
            mURI.setScheme("http");
        }

        if (mURI.getHost().empty()) {
            mURI.setHost("localhost");
        }

        if (mURI.getPort() == 0) {
            mURI.setPort(cDefaultPort);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Fileserver::TranslateFilePathURL(const String& filePath, String& outURL)
{
    if (mURI.getScheme().empty() || mURI.getHost().empty() || mURI.getPort() == 0) {
        return Error(ErrorEnum::eWrongState, "server is not started");
    }

    try {
        auto uri = mURI;

        uri.setPath(fs::relative(fs::path(filePath.CStr()), mRootDir).string());

        outURL = uri.toString().c_str();

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }
}

/***********************************************************************************************************************
 * FileRequestHandler
 **********************************************************************************************************************/

Fileserver::FileRequestHandler::FileRequestHandler(const std::string& rootDir)
    : mRootDir(rootDir)
{
}

void Fileserver::FileRequestHandler::handleRequest(
    [[maybe_unused]] Poco::Net::HTTPServerRequest& request, [[maybe_unused]] Poco::Net::HTTPServerResponse& response)
{
    try {
        std::string path = request.getURI();

        auto queryPos = path.find('?');
        if (queryPos != std::string::npos) {
            path.resize(queryPos);
        }

        Poco::Path fullPath(mRootDir);
        fullPath.append(path);

        Poco::File file(fullPath.toString());
        if (!file.exists() || !file.isFile()) {
            response.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
            response.send();

            return;
        }

        response.setContentType(GetMimeType(fullPath.getExtension()));
        response.setContentLength(file.getSize());

        response.set("Last-Modified",
            Poco::DateTimeFormatter::format(file.getLastModified(), Poco::DateTimeFormat::HTTP_FORMAT));

        Poco::FileInputStream fis(fullPath.toString());
        Poco::StreamCopier::copyStream(fis, response.send());
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to handle request" << common::utils::ToAosError(e);

        response.setStatus(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
        response.send();
    }
}

/***********************************************************************************************************************
 * FileRequestHandlerFactory
 **********************************************************************************************************************/

Fileserver::FileRequestHandlerFactory::FileRequestHandlerFactory(const std::string& rootDir)
    : mRootDir(rootDir)
{
}

Poco::Net::HTTPRequestHandler* Fileserver::FileRequestHandlerFactory::createRequestHandler(
    [[maybe_unused]] const Poco::Net::HTTPServerRequest& request)
{
    return new FileRequestHandler(mRootDir);
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error Fileserver::Start()
{
    if (mThread.joinable()) {
        return Error(ErrorEnum::eFailed, "Server is already running");
    }

    mThread = std::thread([this]() {
        try {
            mServer = std::make_unique<Poco::Net::HTTPServer>(new FileRequestHandlerFactory(mRootDir),
                Poco::Net::ServerSocket(mURI.getPort()), new Poco::Net::HTTPServerParams);

            mServer->start();
        } catch (const std::exception& e) {
            LOG_ERR() << "Failed to start server" << common::utils::ToAosError(e);
        }
    });

    return ErrorEnum::eNone;
}

Error Fileserver::Stop()
{
    if (mServer) {
        mServer->stop();
    }

    if (mThread.joinable()) {
        mThread.join();
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::fileserver
