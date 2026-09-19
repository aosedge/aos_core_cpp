/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <filesystem>
#include <map>

#include <Poco/DateTimeFormat.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/File.h>
#include <Poco/FileStream.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/SecureServerSocket.h>
#include <Poco/Path.h>
#include <Poco/StreamCopier.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/cryptohelper.hpp>
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

Error Fileserver::Init(const std::string& serverURL, const std::string& rootDir, std::string_view certStorage,
    std::string_view caCert, aos::iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider)
{
    try {
        LOG_DBG() << "Init fileserver";

        std::string uri = serverURL;

        if (auto pos = serverURL.find("://"); pos == std::string::npos) {
            uri = "https://" + serverURL;
        }

        Poco::URI parsedURI(uri);
        if (parsedURI.getScheme() != "https") {
            return Error(ErrorEnum::eInvalidArgument, "file server requires HTTPS");
        }

        mRootDir = rootDir;
        mURI     = parsedURI;

        if (mURI.getHost().empty()) {
            mURI.setHost("localhost");
        }

        if (mURI.getPort() == 0) {
            mURI.setPort(cDefaultPort);
        }

        mCertStorage    = certStorage;
        mCACert         = caCert;
        mCertProvider   = &certProvider;
        mCertLoader     = &certLoader;
        mCryptoProvider = &cryptoProvider;

        LOG_DBG() << "Fileserver initialized on" << Log::Field("serverURL", mURI.toString().c_str())
                  << Log::Field("rootDir", rootDir.c_str());
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
    if (mServer) {
        return Error(ErrorEnum::eWrongState, "server is already running");
    }

    if (!mCertProvider || !mCertLoader || !mCryptoProvider) {
        return Error(ErrorEnum::eWrongState, "server is not initialized");
    }

    try {
        auto context = Poco::makeAuto<Poco::Net::Context>(
            Poco::Net::Context::TLS_SERVER_USE, "", Poco::Net::Context::VERIFY_STRICT);

        // Authenticate clients by their certificate chain, not by the connection source IP.
        context->enableExtendedCertificateVerification(false);

        if (auto err = common::utils::ConfigureSSLContext(mCertStorage.c_str(), mCACert.c_str(), *mCertProvider,
                *mCertLoader, *mCryptoProvider, context->sslContext());
            !err.IsNone()) {
            return err;
        }

        Poco::Net::SecureServerSocket             socket(mURI.getPort(), 64, context);
        Poco::Net::HTTPRequestHandlerFactory::Ptr factory = new FileRequestHandlerFactory(mRootDir);
        auto                                      params  = Poco::makeAuto<Poco::Net::HTTPServerParams>();
        auto server = std::make_unique<Poco::Net::HTTPServer>(factory, socket, params);

        server->start();
        mServer = std::move(server);

        LOG_INF() << "Fileserver started on" << Log::Field("serverURL", mURI.toString().c_str());
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error Fileserver::Stop()
{
    try {
        if (mServer) {
            mServer->stopAll(true);
            mServer.reset();
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::fileserver
