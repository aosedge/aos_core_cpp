/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <future>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>

#include <Poco/Net/Context.h>
#include <Poco/Net/SecureServerSocket.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <core/common/tests/mocks/alertsmock.hpp>
#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/mocks/cryptomock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/iam/tests/mocks/certloadermock.hpp>

#include <common/downloader/downloader.hpp>

#include "stubs/httpserverstub.hpp"

using namespace testing;

namespace {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

using URLArray = aos::StaticArray<aos::StaticString<aos::cURLLen>, 4>;

/***********************************************************************************************************************
 * Utils
 **********************************************************************************************************************/

std::unique_ptr<URLArray> CreateURLs(std::initializer_list<std::string> urls)
{
    auto result = std::make_unique<URLArray>();

    for (const auto& url : urls) {
        EXPECT_TRUE(result->EmplaceBack(url.c_str()).IsNone());
    }

    return result;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class DownloaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        std::filesystem::create_directory("download");

        std::ofstream ofs("test_file.dat", std::ios::binary);
        ofs << "This is a test file";
    }

    void CreateLargeFile(const std::string& filename, size_t sizeMB)
    {
        std::ofstream ofs(filename, std::ios::binary);
        if (!ofs.is_open()) {
            FAIL() << "Failed to create large file: " << filename;
        }

        const size_t      bufferSize = 1024 * 1024;
        std::vector<char> buffer(bufferSize);

        for (size_t i = 0; i < bufferSize; ++i) {
            buffer[i] = static_cast<char>(i % 256);
        }

        for (size_t mb = 0; mb < sizeMB; ++mb) {
            ofs.write(buffer.data(), bufferSize);
            if (!ofs.good()) {
                FAIL() << "Failed to write to large file at MB " << mb;
            }
        }
    }

    void StartServer(const std::string& filename = "test_file.dat", int port = 8000, int delayMs = 0)
    {
        mServer.emplace(filename, port, delayMs);
        mServer->Start();
    }

    void StopServer() { mServer->Stop(); }

    void TearDown() override
    {
        std::remove("test_file.dat");
        std::remove("large_test_file.dat");
        std::remove(mFilePath.c_str());
    }

    std::optional<HTTPServer>           mServer;
    aos::common::downloader::Downloader mDownloader;
    aos::CancelContext                  mCancelCtx;
    StrictMock<aos::alerts::SenderMock> mAlertSender;
    std::string                         mFilePath = "download/test_file.dat";
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(DownloaderTest, Download)
{
    StartServer();

    auto err = mDownloader.Download(
        mCancelCtx, "digest1", *CreateURLs({"http://localhost:8000/test_file.dat"}), mFilePath.c_str());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_TRUE(std::filesystem::exists(mFilePath));

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "This is a test file");

    StopServer();
}

TEST_F(DownloaderTest, DownloadFileScheme)
{
    auto err = mDownloader.Download(mCancelCtx, "digest2", *CreateURLs({"file://test_file.dat"}), mFilePath.c_str());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_TRUE(std::filesystem::exists(mFilePath));

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "This is a test file");
}

TEST_F(DownloaderTest, DownloadLargeFileWithProgress)
{
    const size_t fileSizeMB = 5;

    CreateLargeFile("large_test_file.dat", fileSizeMB);

    mDownloader.Init(&mAlertSender, std::chrono::seconds {1});

    StartServer("large_test_file.dat", 8001, 350);

    EXPECT_CALL(mAlertSender, SendAlert(_)).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    auto err = mDownloader.Download(
        mCancelCtx, "digest3", *CreateURLs({"http://localhost:8001/large_test_file.dat"}), mFilePath.c_str());

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(mFilePath));

    std::ifstream ifs(mFilePath, std::ios::binary | std::ios::ate);
    auto          fileSize = ifs.tellg();

    EXPECT_EQ(fileSize, static_cast<std::streamsize>(fileSizeMB * 1024 * 1024));

    StopServer();
}

TEST_F(DownloaderTest, ParallelDownloads)
{
    CreateLargeFile("large_test_file.dat", 2);

    StartServer("large_test_file.dat", 8003, 100);

    std::future<aos::Error> download1 = std::async(std::launch::async, [this]() {
        return mDownloader.Download(mCancelCtx, "digest_parallel_1",
            *CreateURLs({"http://localhost:8003/large_test_file.dat"}), "download/file1.dat");
    });

    std::future<aos::Error> download2 = std::async(std::launch::async, [this]() {
        return mDownloader.Download(mCancelCtx, "digest_parallel_2",
            *CreateURLs({"http://localhost:8003/large_test_file.dat"}), "download/file2.dat");
    });

    auto err1 = download1.get();
    auto err2 = download2.get();

    EXPECT_EQ(err1, aos::ErrorEnum::eNone);
    EXPECT_EQ(err2, aos::ErrorEnum::eNone);

    EXPECT_TRUE(std::filesystem::exists("download/file1.dat"));
    EXPECT_TRUE(std::filesystem::exists("download/file2.dat"));

    std::remove("download/file1.dat");
    std::remove("download/file2.dat");

    StopServer();
}

TEST_F(DownloaderTest, DuplicateDownload)
{
    CreateLargeFile("large_test_file.dat", 5);

    StartServer("large_test_file.dat", 8004, 100);

    std::future<aos::Error> download1 = std::async(std::launch::async, [this]() {
        return mDownloader.Download(mCancelCtx, "digest_dup",
            *CreateURLs({"http://localhost:8004/large_test_file.dat"}), "download/file_dup.dat");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto err2 = mDownloader.Download(
        mCancelCtx, "digest_dup", *CreateURLs({"http://localhost:8004/large_test_file.dat"}), "download/file_dup2.dat");
    EXPECT_EQ(err2, aos::ErrorEnum::eAlreadyExist);

    auto err1 = download1.get();
    EXPECT_EQ(err1, aos::ErrorEnum::eNone);

    std::remove("download/file_dup.dat");

    StopServer();
}

TEST_F(DownloaderTest, MutualTLSRejectsUnencryptedURLs)
{
    StrictMock<aos::iamclient::CertProviderMock> certProvider;
    StrictMock<aos::crypto::CertLoaderMock>      certLoader;
    StrictMock<aos::crypto::x509::ProviderMock>  cryptoProvider;

    ASSERT_TRUE(mDownloader.Init("sm", "ca.pem", certProvider, certLoader, cryptoProvider).IsNone());

    EXPECT_TRUE(
        mDownloader
            .Download(mCancelCtx, "http", *CreateURLs({"http://localhost:8000/test_file.dat"}), mFilePath.c_str())
            .Is(aos::ErrorEnum::eInvalidArgument));
    EXPECT_TRUE(mDownloader.Download(mCancelCtx, "file", *CreateURLs({"file://test_file.dat"}), mFilePath.c_str())
                    .Is(aos::ErrorEnum::eInvalidArgument));
    EXPECT_FALSE(std::filesystem::exists(mFilePath));
}

TEST_F(DownloaderTest, MutualTLSRequiresCA)
{
    StrictMock<aos::iamclient::CertProviderMock> certProvider;
    StrictMock<aos::crypto::CertLoaderMock>      certLoader;
    StrictMock<aos::crypto::x509::ProviderMock>  cryptoProvider;

    EXPECT_TRUE(
        mDownloader.Init("sm", "", certProvider, certLoader, cryptoProvider).Is(aos::ErrorEnum::eInvalidArgument));
}

TEST_F(DownloaderTest, MutualTLSDoesNotChangeOtherDownloaders)
{
    StrictMock<aos::iamclient::CertProviderMock> certProvider;
    StrictMock<aos::crypto::CertLoaderMock>      certLoader;
    StrictMock<aos::crypto::x509::ProviderMock>  cryptoProvider;

    ASSERT_TRUE(mDownloader.Init("sm", "ca.pem", certProvider, certLoader, cryptoProvider).IsNone());

    aos::common::downloader::Downloader downloader;
    ASSERT_TRUE(downloader.Init().IsNone());
    EXPECT_TRUE(
        downloader.Download(mCancelCtx, "file", *CreateURLs({"file://test_file.dat"}), mFilePath.c_str()).IsNone());
}

class DownloaderTLSFailureTest : public DownloaderTest, public ::testing::WithParamInterface<int> { };

TEST_P(DownloaderTLSFailureTest, CredentialFailureNeverDownloadsUnverifiedContent)
{
    StrictMock<aos::iamclient::CertProviderMock> certProvider;
    StrictMock<aos::crypto::CertLoaderMock>      certLoader;
    StrictMock<aos::crypto::x509::ProviderMock>  cryptoProvider;

    const std::string ca      = CERTIFICATES_DIR "/ca.pem";
    auto              context = Poco::makeAuto<Poco::Net::Context>(Poco::Net::Context::TLS_SERVER_USE,
        CERTIFICATES_DIR "/server.key", CERTIFICATES_DIR "/server.cer", ca, Poco::Net::Context::VERIFY_STRICT);
    context->enableExtendedCertificateVerification(false);
    Poco::Net::SecureServerSocket             socket(0, 64, context);
    Poco::Net::HTTPRequestHandlerFactory::Ptr factory = new FileRequestHandlerFactory("test_file.dat");
    auto                                      params  = Poco::makeAuto<Poco::Net::HTTPServerParams>();
    Poco::Net::HTTPServer                     server(factory, socket, params);
    server.start();

    ASSERT_TRUE(mDownloader.Init("sm", ca, certProvider, certLoader, cryptoProvider).IsNone());
    EXPECT_CALL(certProvider, GetCert(_, _, _, _))
        .Times(3)
        .WillRepeatedly(Invoke([&](const aos::String& certType, const aos::Array<uint8_t>&, const aos::Array<uint8_t>&,
                                   aos::CertInfo&) -> aos::Error {
            EXPECT_EQ(certType, "sm");
            switch (GetParam()) {
            case 1:
                throw std::runtime_error("IAM unavailable");
            case 2:
                throw 42;
            default:
                return aos::ErrorEnum::eNotFound;
            }
        }));

    std::ofstream(mFilePath) << "partial download";
    auto url = "https://localhost:" + std::to_string(socket.address().port()) + "/test_file.dat";
    EXPECT_FALSE(mDownloader.Download(mCancelCtx, "tls_failure", *CreateURLs({url}), mFilePath.c_str()).IsNone());

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "partial download");

    server.stopAll(true);
}

INSTANTIATE_TEST_SUITE_P(Credentials, DownloaderTLSFailureTest, Values(0, 1, 2));

TEST_F(DownloaderTest, MutualTLSRejectsMalformedURL)
{
    StrictMock<aos::iamclient::CertProviderMock> certProvider;
    StrictMock<aos::crypto::CertLoaderMock>      certLoader;
    StrictMock<aos::crypto::x509::ProviderMock>  cryptoProvider;
    ASSERT_TRUE(mDownloader.Init("sm", "ca.pem", certProvider, certLoader, cryptoProvider).IsNone());
    EXPECT_FALSE(
        mDownloader.Download(mCancelCtx, "invalid", *CreateURLs({"https://[invalid"}), mFilePath.c_str()).IsNone());
    EXPECT_FALSE(std::filesystem::exists(mFilePath));
}

TEST_F(DownloaderTest, FallbackToNextURL)
{
    StartServer("test_file.dat", 8005);

    auto err = mDownloader.Download(mCancelCtx, "digest_fallback",
        *CreateURLs({"http://127.0.0.1:1/test_file.dat", "http://localhost:8005/test_file.dat"}), mFilePath.c_str());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "This is a test file");

    StopServer();
}

TEST_F(DownloaderTest, AllURLsFailReturnsLastError)
{
    StartServer("missing_file.dat", 8006);

    auto err = mDownloader.Download(mCancelCtx, "digest_all_fail",
        *CreateURLs({"file://missing_file.dat", "http://localhost:8006/missing_file.dat"}), mFilePath.c_str());
    EXPECT_EQ(err.Code(), 404);

    StopServer();
}

TEST_F(DownloaderTest, EmptyURLs)
{
    auto err = mDownloader.Download(mCancelCtx, "digest_empty", *CreateURLs({}), mFilePath.c_str());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eInvalidArgument));
}

TEST_F(DownloaderTest, PreCanceledContext)
{
    ASSERT_TRUE(mCancelCtx.Cancel().IsNone());

    auto err = mDownloader.Download(
        mCancelCtx, "digest_precanceled", *CreateURLs({"file://test_file.dat"}), mFilePath.c_str());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eCanceled));
    EXPECT_FALSE(std::filesystem::exists(mFilePath));

    aos::CancelContext ctx;

    err = mDownloader.Download(ctx, "digest_precanceled", *CreateURLs({"file://test_file.dat"}), mFilePath.c_str());
    EXPECT_TRUE(err.IsNone());
}

TEST_F(DownloaderTest, CancelDuringRetryDelay)
{
    auto downloadFuture = std::async(std::launch::async, [this]() {
        return mDownloader.Download(mCancelCtx, "digest_cancel_delay",
            *CreateURLs({"http://127.0.0.1:1/test_file.dat", "file://test_file.dat"}), mFilePath.c_str());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(mCancelCtx.Cancel().IsNone());
    ASSERT_EQ(downloadFuture.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);

    EXPECT_TRUE(downloadFuture.get().Is(aos::ErrorEnum::eCanceled));
    EXPECT_TRUE(!std::filesystem::exists(mFilePath) || std::filesystem::file_size(mFilePath) == 0);
}

TEST_F(DownloaderTest, CancelDuringTransfer)
{
    const size_t fileSizeMB = 10;

    CreateLargeFile("large_test_file.dat", fileSizeMB);

    StartServer("large_test_file.dat", 8007, 100);

    auto downloadFuture = std::async(std::launch::async, [this]() {
        return mDownloader.Download(mCancelCtx, "digest_cancel_transfer",
            *CreateURLs({"http://localhost:8007/large_test_file.dat", "file://large_test_file.dat"}),
            mFilePath.c_str());
    });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (!std::filesystem::exists(mFilePath) || std::filesystem::file_size(mFilePath) == 0) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(mCancelCtx.Cancel().IsNone());
    ASSERT_EQ(downloadFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    EXPECT_TRUE(downloadFuture.get().Is(aos::ErrorEnum::eCanceled));

    auto size = std::filesystem::file_size(mFilePath);

    EXPECT_GT(size, 0);
    EXPECT_LT(size, fileSizeMB * 1024 * 1024);

    StopServer();
}

TEST_F(DownloaderTest, HeadFailureKeepsPartialFile)
{
    std::atomic<int>                          headCount {0};
    std::atomic<int>                          getCount {0};
    Poco::Net::ServerSocket                   socket(0);
    Poco::Net::HTTPRequestHandlerFactory::Ptr factory = new FunctionRequestHandlerFactory(
        [&](Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) {
            if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_HEAD) {
                if (++headCount == 2) {
                    EXPECT_TRUE(mCancelCtx.Cancel().IsNone());
                }
            } else {
                ++getCount;
            }

            response.setStatus(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
            response.send();
        });
    Poco::Net::HTTPServer server(factory, socket, new Poco::Net::HTTPServerParams);

    server.start();

    std::ofstream(mFilePath, std::ios::binary) << "partial download";

    auto url = "http://127.0.0.1:" + std::to_string(socket.address().port()) + "/test_file.dat";
    auto err = mDownloader.Download(mCancelCtx, "digest_head_failure", *CreateURLs({url}), mFilePath.c_str());

    EXPECT_TRUE(err.Is(aos::ErrorEnum::eCanceled));
    EXPECT_EQ(headCount, 2);
    EXPECT_EQ(getCount, 0);

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "partial download");

    server.stopAll(true);
}

TEST_F(DownloaderTest, ResumesWithLowercaseAcceptRanges)
{
    const std::string                         fileContent = "This is a test file";
    const size_t                              partialSize = 8;
    std::atomic<bool>                         rangeRequested {false};
    Poco::Net::ServerSocket                   socket(0);
    Poco::Net::HTTPRequestHandlerFactory::Ptr factory = new FunctionRequestHandlerFactory(
        [&](Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) {
            if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_HEAD) {
                response.set("accept-ranges", "bytes");
                response.setContentLength(fileContent.size());
                response.send();

                return;
            }

            if (request.get("Range", "") != "bytes=" + std::to_string(partialSize) + "-") {
                response.setContentLength(fileContent.size());
                response.send() << fileContent;

                return;
            }

            rangeRequested = true;

            response.setStatus(Poco::Net::HTTPResponse::HTTP_PARTIAL_CONTENT);
            response.set("Content-Range",
                "bytes " + std::to_string(partialSize) + "-" + std::to_string(fileContent.size() - 1) + "/"
                    + std::to_string(fileContent.size()));
            response.setContentLength(fileContent.size() - partialSize);
            response.send() << fileContent.substr(partialSize);
        });
    Poco::Net::HTTPServer server(factory, socket, new Poco::Net::HTTPServerParams);

    server.start();

    std::ofstream(mFilePath, std::ios::binary) << fileContent.substr(0, partialSize);

    auto url = "http://127.0.0.1:" + std::to_string(socket.address().port()) + "/test_file.dat";
    auto err = mDownloader.Download(mCancelCtx, "digest_lowercase_ranges", *CreateURLs({url}), mFilePath.c_str());

    EXPECT_TRUE(err.IsNone());
    EXPECT_TRUE(rangeRequested);

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, fileContent);

    server.stopAll(true);
}

TEST_F(DownloaderTest, DownloadFileSchemeDirectoryFails)
{
    std::filesystem::create_directory("source_dir");

    auto err = mDownloader.Download(mCancelCtx, "digest_dir", *CreateURLs({"file://source_dir"}), mFilePath.c_str());

    EXPECT_FALSE(err.IsNone());

    std::filesystem::remove_all("source_dir");
}

TEST_F(DownloaderTest, DownloadFileSchemeEmptyFile)
{
    std::ofstream("empty_file.dat", std::ios::binary);
    std::ofstream(mFilePath, std::ios::binary) << "previous content";

    auto err = mDownloader.Download(
        mCancelCtx, "digest_empty_file", *CreateURLs({"file://empty_file.dat"}), mFilePath.c_str());

    EXPECT_TRUE(err.IsNone());
    EXPECT_TRUE(std::filesystem::exists(mFilePath));
    EXPECT_EQ(std::filesystem::file_size(mFilePath), 0);

    std::remove("empty_file.dat");
}
