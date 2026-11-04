/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cerrno>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Poco/DateTimeFormat.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/SecureServerSocket.h>
#include <Poco/Timestamp.h>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>

#include <common/utils/cryptohelper.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/httpstatus.hpp>

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

constexpr size_t cReadChunkSize = 64 * 1024;

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

bool IsIntegrityError(int err)
{
    return err == EIO || err == EBADMSG || err == EUCLEAN || err == EILSEQ;
}

int StatusFromErrno(int err)
{
    if (IsIntegrityError(err)) {
        return common::utils::cIntegrityHTTPStatus;
    }

    if (err == ENOENT || err == ENOTDIR) {
        return Poco::Net::HTTPResponse::HTTP_NOT_FOUND;
    }

    return Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
}

ssize_t ReadChunk(const Fileserver::ReadFunction& readFunc, int fd, std::vector<char>& buffer)
{
    ssize_t size = 0;

    do {
        size = readFunc(fd, buffer.data(), buffer.size());
    } while (size < 0 && errno == EINTR);

    return size;
}

void SendStatus(Poco::Net::HTTPServerResponse& response, int status)
{
    if (status == common::utils::cIntegrityHTTPStatus) {
        response.setStatus(std::to_string(status));
        response.setReason(common::utils::cIntegrityHTTPReason);
    } else {
        response.setStatus(static_cast<Poco::Net::HTTPResponse::HTTPStatus>(status));
    }

    response.send();
}

} // namespace

/***********************************************************************************************************************
 * BrokenFiles
 **********************************************************************************************************************/

void BrokenFiles::Mark(const std::string& path, const struct timespec& mtime)
{
    std::lock_guard lock {mMutex};

    mFiles[path] = mtime;
}

bool BrokenFiles::Take(const std::string& path, const struct stat& st)
{
    std::lock_guard lock {mMutex};

    auto it = mFiles.find(path);
    if (it == mFiles.end()) {
        return false;
    }

    auto matched = st.st_mtim.tv_sec == it->second.tv_sec && st.st_mtim.tv_nsec == it->second.tv_nsec;

    mFiles.erase(it);

    return matched;
}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Fileserver::Fileserver(SSLContextConfigurator configureContext, std::chrono::milliseconds retryInterval)
    : mConfigureContext(std::move(configureContext))
    , mRetryInterval(retryInterval)
{
}

Fileserver::~Fileserver()
{
    if (auto err = Stop(); !err.IsNone()) {
        LOG_ERR() << "Failed to stop file server" << Log::Field(err);
    }
}

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

Fileserver::FileRequestHandler::FileRequestHandler(
    const std::string& rootDir, std::shared_ptr<BrokenFiles> brokenFiles, ReadFunction read)
    : mRootDir(rootDir)
    , mBrokenFiles(std::move(brokenFiles))
    , mRead(std::move(read))
{
}

void Fileserver::FileRequestHandler::handleRequest(
    Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
    try {
        std::string target = request.getURI();

        auto queryPos = target.find('?');
        if (queryPos != std::string::npos) {
            target.resize(queryPos);
        }

        fs::path filePath;

        if (!ResolvePath(target, filePath)) {
            LOG_ERR() << "Request target is outside root directory" << Log::Field("target", target.c_str());

            SendStatus(response, Poco::Net::HTTPResponse::HTTP_NOT_FOUND);

            return;
        }

        const auto  path = filePath.string();
        struct stat st { };

        auto fd = OpenFile(path, st, response);
        if (!fd) {
            return;
        }

        auto extension = filePath.extension().string();
        if (!extension.empty()) {
            extension.erase(0, 1);
        }

        SendFile(fd.get().mFD, path, st, extension, request, response);
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to handle request" << common::utils::ToAosError(e);

        if (!response.sent()) {
            SendStatus(response, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
        }
    }
}

/***********************************************************************************************************************
 * FileRequestHandler private
 **********************************************************************************************************************/

bool Fileserver::FileRequestHandler::ResolvePath(const std::string& target, fs::path& path) const
{
    auto root = fs::path(mRootDir).lexically_normal();
    if (!root.has_filename()) {
        root = root.parent_path();
    }

    path = (root / fs::path(target).relative_path()).lexically_normal();

    auto relative = path.lexically_relative(root);

    return !relative.empty() && *relative.begin() != "..";
}

Fileserver::FileRequestHandler::FDPtr Fileserver::FileRequestHandler::OpenFile(
    const std::string& path, struct stat& st, Poco::Net::HTTPServerResponse& response)
{
    FDPtr fd(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
    if (!fd) {
        auto err = errno;

        if (err != ENOENT) {
            LOG_ERR() << "Failed to open file" << Log::Field("path", path.c_str())
                      << Log::Field(Error(err, "can't open file"));
        }

        SendStatus(response, StatusFromErrno(err));

        return {};
    }

    if (fstat(fd.get().mFD, &st) != 0) {
        auto err = errno;

        LOG_ERR() << "Failed to stat file" << Log::Field("path", path.c_str())
                  << Log::Field(Error(err, "can't stat file"));

        SendStatus(response, StatusFromErrno(err));

        return {};
    }

    if (!S_ISREG(st.st_mode)) {
        SendStatus(response, Poco::Net::HTTPResponse::HTTP_NOT_FOUND);

        return {};
    }

    if (mBrokenFiles->Take(path, st)) {
        LOG_DBG() << "File is marked as broken" << Log::Field("path", path.c_str());

        SendStatus(response, common::utils::cIntegrityHTTPStatus);

        return {};
    }

    return fd;
}

void Fileserver::FileRequestHandler::HandleReadError(
    const std::string& path, const struct stat& st, int err, Poco::Net::HTTPServerResponse& response)
{
    LOG_ERR() << "Failed to read file" << Log::Field("path", path.c_str()) << Log::Field("sent", response.sent())
              << Log::Field(Error(err, "can't read file"));

    if (!response.sent()) {
        SendStatus(response, StatusFromErrno(err));
    } else if (IsIntegrityError(err)) {
        mBrokenFiles->Mark(path, st.st_mtim);
    }
}

void Fileserver::FileRequestHandler::SendFile(int fd, const std::string& path, const struct stat& st,
    const std::string& extension, Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
    std::vector<char> buffer(cReadChunkSize);

    auto size = ReadChunk(mRead, fd, buffer);
    if (size < 0) {
        HandleReadError(path, st, errno, response);

        return;
    }

    response.setContentType(GetMimeType(extension));
    response.setContentLength64(st.st_size);
    response.set("Last-Modified",
        Poco::DateTimeFormatter::format(
            Poco::Timestamp::fromEpochTime(st.st_mtim.tv_sec), Poco::DateTimeFormat::HTTP_FORMAT));
    response.setKeepAlive(false);

    auto& out = response.send();

    if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_HEAD) {
        return;
    }

    while (size > 0) {
        if (!out.write(buffer.data(), size)) {
            LOG_ERR() << "Failed to send file" << Log::Field("path", path.c_str());

            return;
        }

        size = ReadChunk(mRead, fd, buffer);
    }

    if (size < 0) {
        HandleReadError(path, st, errno, response);
    }
}

/***********************************************************************************************************************
 * FileRequestHandlerFactory
 **********************************************************************************************************************/

Fileserver::FileRequestHandlerFactory::FileRequestHandlerFactory(const std::string& rootDir, ReadFunction read)
    : mRootDir(rootDir)
    , mRead(std::move(read))
    , mBrokenFiles(std::make_shared<BrokenFiles>())
{
}

Poco::Net::HTTPRequestHandler* Fileserver::FileRequestHandlerFactory::createRequestHandler(
    [[maybe_unused]] const Poco::Net::HTTPServerRequest& request)
{
    return new FileRequestHandler(mRootDir, mBrokenFiles, mRead);
}

Error Fileserver::Start()
{
    if (!mCertProvider || !mCertLoader || !mCryptoProvider) {
        return Error(ErrorEnum::eWrongState, "server is not initialized");
    }

    {
        std::lock_guard lock {mMutex};

        if (mServer || mCertUpdateThread.joinable()) {
            return Error(ErrorEnum::eWrongState, "server is already running");
        }

        mShutdown    = false;
        mCertChanged = false;
    }

    auto cleanup = DeferRelease(this, [](Fileserver* server) {
        if (auto err = server->Stop(); !err.IsNone()) {
            LOG_ERR() << "Failed to clean up file server" << Log::Field(err);
        }
    });

    Poco::Net::Context::Ptr context;
    if (auto err = CreateSSLContext(context); !err.IsNone()) {
        return err;
    }

    if (auto err = StartServer(context); !err.IsNone()) {
        return err;
    }

    if (auto err = mCertProvider->SubscribeListener(mCertStorage.c_str(), *this); !err.IsNone()) {
        return err;
    }

    try {
        mCertUpdateThread = std::thread(&Fileserver::ProcessCertificateChanges, this);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    static_cast<void>(cleanup.Release());

    return ErrorEnum::eNone;
}

Error Fileserver::Stop()
{
    {
        std::lock_guard lock {mMutex};

        mShutdown = true;
    }

    mCondVar.notify_all();

    if (mCertUpdateThread.joinable()) {
        mCertUpdateThread.join();
    }

    auto err = StopServer();

    if (mCertProvider) {
        if (auto unsubscribeErr = mCertProvider->UnsubscribeListener(*this); !unsubscribeErr.IsNone()) {
            LOG_ERR() << "Failed to unsubscribe file server from certificate changes" << Log::Field(unsubscribeErr);
        }
    }

    return err;
}

void Fileserver::OnCertChanged([[maybe_unused]] const CertInfo& info)
{
    {
        std::lock_guard lock {mMutex};

        if (mShutdown) {
            return;
        }

        mCertChanged = true;
    }

    mCondVar.notify_one();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Fileserver::ProcessCertificateChanges()
{
    std::unique_lock lock {mMutex};

    while (true) {
        mCondVar.wait(lock, [this]() { return mShutdown || mCertChanged; });

        if (mShutdown) {
            return;
        }

        mCertChanged = false;
        lock.unlock();

        auto err = ReloadCertificate();

        lock.lock();

        if (!err.IsNone()) {
            LOG_ERR() << "Failed to reload file server certificate, retrying" << Log::Field(err);

            static_cast<void>(mCondVar.wait_for(lock, mRetryInterval, [this]() { return mShutdown || mCertChanged; }));
            mCertChanged = true;
        }
    }
}

Error Fileserver::ReloadCertificate()
{
    Poco::Net::Context::Ptr context;
    auto                    err = CreateSSLContext(context);

    {
        std::lock_guard lock {mMutex};

        // Discard credentials or a loading error superseded by a newer notification.
        if (mShutdown || mCertChanged) {
            return ErrorEnum::eWrongState;
        }
    }

    if (!err.IsNone()) {
        return err;
    }

    if (auto stopErr = StopServer(); !stopErr.IsNone()) {
        return stopErr;
    }

    return StartServer(context);
}

Error Fileserver::CreateSSLContext(Poco::Net::Context::Ptr& context)
{
    try {
        context = Poco::makeAuto<Poco::Net::Context>(
            Poco::Net::Context::TLS_SERVER_USE, "", Poco::Net::Context::VERIFY_STRICT);

        // Authenticate clients by their certificate chain, not by the connection source IP.
        context->enableExtendedCertificateVerification(false);

        if (mConfigureContext) {
            return mConfigureContext(context->sslContext());
        }

        return common::utils::ConfigureSSLContext(mCertStorage.c_str(), mCACert.c_str(), *mCertProvider, *mCertLoader,
            *mCryptoProvider, context->sslContext());
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }
}

Error Fileserver::StartServer(const Poco::Net::Context::Ptr& context)
{
    std::lock_guard lock {mMutex};

    if (mShutdown || mCertChanged) {
        return ErrorEnum::eWrongState;
    }

    try {
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

Error Fileserver::StopServer()
{
    std::unique_ptr<Poco::Net::HTTPServer> server;

    {
        std::lock_guard lock {mMutex};

        server = std::move(mServer);
    }

    try {
        if (server) {
            server->stopAll(true);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::fileserver
