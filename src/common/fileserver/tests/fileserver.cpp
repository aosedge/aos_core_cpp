/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <stdexcept>

#include <gtest/gtest.h>

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/StreamCopier.h>
#include <openssl/ssl.h>

#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/mocks/cryptomock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tools/string.hpp>
#include <core/iam/tests/mocks/certloadermock.hpp>

#include <common/fileserver/fileserver.hpp>

using namespace testing;

namespace aos::common::fileserver::test {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CommonFileserverTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();
        EXPECT_CALL(mCertProvider, UnsubscribeListener(Ref(mFileserver)))
            .Times(AnyNumber())
            .WillRepeatedly(Return(ErrorEnum::eNone));
        ASSERT_TRUE(
            mFileserver.Init("localhost:8000", "download", "cm", "ca.pem", mCertProvider, mCertLoader, mCryptoProvider)
                .IsNone());
    }

    StrictMock<aos::iamclient::CertProviderMock> mCertProvider;
    StrictMock<crypto::CertLoaderMock>           mCertLoader;
    StrictMock<crypto::x509::ProviderMock>       mCryptoProvider;
    Fileserver                                   mFileserver;
};

// Exercise file response handling independently of the production mTLS listener.
class FileRequestHandlerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();
        std::filesystem::create_directory("download");
        Poco::Net::ServerSocket socket(0);
        mPort   = socket.address().port();
        mServer = std::make_unique<Poco::Net::HTTPServer>(
            new Fileserver::FileRequestHandlerFactory("download"), socket, new Poco::Net::HTTPServerParams);
        mServer->start();
    }

    void TearDown() override
    {
        mServer->stopAll(true);
        mServer.reset();
        std::filesystem::remove_all("download");
    }

    Poco::UInt16                           mPort {};
    std::unique_ptr<Poco::Net::HTTPServer> mServer;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CommonFileserverTest, TranslateFilePathURL)
{
    StaticString<256> url;

    auto err = mFileserver.TranslateFilePathURL("download/test_file.dat", url);
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(url, "https://localhost:8000/test_file.dat");
}

TEST_F(CommonFileserverTest, RejectHTTP)
{
    EXPECT_FALSE(
        mFileserver
            .Init("http://localhost:8000", "download", "cm", "ca.pem", mCertProvider, mCertLoader, mCryptoProvider)
            .IsNone());
}

TEST_F(CommonFileserverTest, StartWithoutInit)
{
    Fileserver server;
    EXPECT_TRUE(server.Start().Is(ErrorEnum::eWrongState));
    EXPECT_TRUE(server.Stop().IsNone());
}

TEST_F(CommonFileserverTest, StartReturnsCertificateErrorAndCanRetry)
{
    EXPECT_CALL(mCertProvider, GetCert(_, _, _, _))
        .Times(2)
        .WillRepeatedly(
            Invoke([](const String& certType, const Array<uint8_t>&, const Array<uint8_t>&, CertInfo&) -> Error {
                EXPECT_EQ(certType, "cm");
                return ErrorEnum::eNotFound;
            }));

    EXPECT_TRUE(mFileserver.Start().Is(ErrorEnum::eNotFound));
    EXPECT_TRUE(mFileserver.Start().Is(ErrorEnum::eNotFound));
    EXPECT_TRUE(mFileserver.Stop().IsNone());
}

TEST_F(CommonFileserverTest, CertificateNotificationAfterStopIsIgnored)
{
    EXPECT_TRUE(mFileserver.Stop().IsNone());

    auto info = std::make_unique<CertInfo>();
    mFileserver.OnCertChanged(*info);

    EXPECT_TRUE(mFileserver.Stop().IsNone());
}

class FileserverRotationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();
        Poco::Net::ServerSocket socket(0);
        mPort = socket.address().port();
        std::filesystem::create_directory("download");
        std::ofstream("download/test.txt") << "secure content";
    }

    void TearDown() override
    {
        if (mServer) {
            EXPECT_TRUE(mServer->Stop().IsNone());
            mServer.reset();
        }
        std::filesystem::remove_all("download");
    }

    void Init(std::chrono::milliseconds retryInterval = std::chrono::seconds(10))
    {
        ON_CALL(mConfigure, Call(_)).WillByDefault(Invoke(this, &FileserverRotationTest::Configure));
        mServer = std::make_unique<Fileserver>(
            [this](SSL_CTX* context) { return mConfigure.Call(context); }, retryInterval);
        ASSERT_TRUE(mServer
                        ->Init("localhost:" + std::to_string(mPort), "download", "cm", "ca.pem", mCertProvider,
                            mCertLoader, mCryptoProvider)
                        .IsNone());
        EXPECT_CALL(mCertProvider, SubscribeListener(_, Ref(*mServer))).WillOnce(Return(ErrorEnum::eNone));
        EXPECT_CALL(mCertProvider, UnsubscribeListener(Ref(*mServer)))
            .Times(AtLeast(1))
            .WillRepeatedly(Return(ErrorEnum::eNone));
    }

    Error Configure(SSL_CTX* context)
    {
        if (SSL_CTX_use_certificate_file(context, mCert.c_str(), SSL_FILETYPE_PEM) != 1
            || SSL_CTX_use_PrivateKey_file(context, mKey.c_str(), SSL_FILETYPE_PEM) != 1
            || SSL_CTX_load_verify_locations(context, mCert.c_str(), nullptr) != 1) {
            return ErrorEnum::eFailed;
        }
        return ErrorEnum::eNone;
    }

    std::string Download(bool clientCertificate = true, std::string* peerName = nullptr)
    {
        auto                          context = Poco::makeAuto<Poco::Net::Context>(Poco::Net::Context::TLS_CLIENT_USE,
            clientCertificate ? mKey : "", clientCertificate ? mCert : "", mCert, Poco::Net::Context::VERIFY_STRICT);
        Poco::Net::HTTPSClientSession session("localhost", mPort, context);
        session.setTimeout(Poco::Timespan(2, 0));
        Poco::Net::HTTPRequest  request(Poco::Net::HTTPRequest::HTTP_GET, "/test.txt");
        Poco::Net::HTTPResponse response;
        session.sendRequest(request);
        std::stringstream contents;
        Poco::StreamCopier::copyStream(session.receiveResponse(response), contents);
        EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);
        if (peerName) {
            *peerName = session.serverCertificate().commonName();
        }
        return contents.str();
    }

    void Notify()
    {
        auto info = std::make_unique<CertInfo>();
        mServer->OnCertChanged(*info);
    }

    const std::string mCert = (std::filesystem::path(__FILE__).parent_path() / "data/localhost.pem").string();
    const std::string mKey  = (std::filesystem::path(__FILE__).parent_path() / "data/localhost.key").string();
    Poco::UInt16      mPort {};
    StrictMock<aos::iamclient::CertProviderMock> mCertProvider;
    StrictMock<crypto::CertLoaderMock>           mCertLoader;
    StrictMock<crypto::x509::ProviderMock>       mCryptoProvider;
    NiceMock<MockFunction<Error(SSL_CTX*)>>      mConfigure;
    std::unique_ptr<Fileserver>                  mServer;
};

TEST_F(FileserverRotationTest, MutualTLSRequiresClientCertificate)
{
    Init();
    ASSERT_TRUE(mServer->Start().IsNone());
    EXPECT_NO_THROW(EXPECT_EQ(Download(), "secure content"));
    EXPECT_ANY_THROW(Download(false));
}

TEST_F(FileserverRotationTest, SuccessfulRotationServesNewCertificate)
{
    Init();
    ASSERT_TRUE(mServer->Start().IsNone());
    std::string peerName;
    ASSERT_EQ(Download(true, &peerName), "secure content");
    ASSERT_EQ(peerName, "localhost");

    EXPECT_CALL(mConfigure, Call(_)).WillOnce(Invoke([&](SSL_CTX* context) -> Error {
        auto err = Configure(context);
        if (!err.IsNone()) {
            return err;
        }
        auto cert = (std::filesystem::path(__FILE__).parent_path() / "data/rotated.pem").string();
        return SSL_CTX_use_certificate_file(context, cert.c_str(), SSL_FILETYPE_PEM) == 1 ? ErrorEnum::eNone
                                                                                          : ErrorEnum::eFailed;
    }));
    Notify();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (peerName != "rotated.localhost" && std::chrono::steady_clock::now() < deadline) {
        try {
            EXPECT_EQ(Download(true, &peerName), "secure content");
        } catch (const Poco::Exception&) {
            // The old listening socket is closed before the replacement binds the same port.
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(peerName, "rotated.localhost");
    EXPECT_TRUE(mServer->Stop().IsNone());
}

TEST_F(FileserverRotationTest, SubscriptionFailureStopsListenerAndCanRetry)
{
    Init();
    EXPECT_CALL(mCertProvider, SubscribeListener(_, Ref(*mServer)))
        .WillOnce(Return(ErrorEnum::eFailed))
        .RetiresOnSaturation();
    EXPECT_TRUE(mServer->Start().Is(ErrorEnum::eFailed));
    EXPECT_ANY_THROW(Download());
    ASSERT_TRUE(mServer->Start().IsNone());
    EXPECT_NO_THROW(EXPECT_EQ(Download(), "secure content"));
}

TEST_F(FileserverRotationTest, RepeatedStopIsSafe)
{
    Init();
    ASSERT_TRUE(mServer->Start().IsNone());
    EXPECT_TRUE(mServer->Stop().IsNone());
    EXPECT_TRUE(mServer->Stop().IsNone());
}

TEST_F(FileserverRotationTest, UnsubscribeErrorDoesNotOverrideStopResult)
{
    Init();
    ASSERT_TRUE(mServer->Start().IsNone());
    EXPECT_CALL(mCertProvider, UnsubscribeListener(Ref(*mServer)))
        .WillOnce(Return(ErrorEnum::eFailed))
        .RetiresOnSaturation();
    EXPECT_TRUE(mServer->Stop().IsNone());
}

TEST_F(FileserverRotationTest, LoadingFailureKeepsListenerAndRetries)
{
    Init(std::chrono::milliseconds(100));
    ASSERT_TRUE(mServer->Start().IsNone());
    std::promise<void> failed;
    std::promise<void> retried;
    auto               failure = failed.get_future();
    auto               retry   = retried.get_future();
    EXPECT_CALL(mConfigure, Call(_))
        .WillOnce(Invoke([&](SSL_CTX*) -> Error {
            failed.set_value();
            return ErrorEnum::eFailed;
        }))
        .WillOnce(Invoke([&](SSL_CTX* context) {
            auto err = Configure(context);
            retried.set_value();
            return err;
        }));
    Notify();
    EXPECT_EQ(failure.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_NO_THROW(EXPECT_EQ(Download(), "secure content"));
    EXPECT_EQ(retry.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(mServer->Stop().IsNone());
}

TEST_F(FileserverRotationTest, NotificationDuringLoadingImmediatelyLoadsLatestContext)
{
    Init();
    ASSERT_TRUE(mServer->Start().IsNone());
    std::promise<void> loading;
    std::promise<void> release;
    std::promise<void> reloaded;
    auto               started = loading.get_future();
    auto               resume  = release.get_future();
    auto               latest  = reloaded.get_future();
    EXPECT_CALL(mConfigure, Call(_))
        .WillOnce(Invoke([&](SSL_CTX*) -> Error {
            loading.set_value();
            resume.wait();
            return ErrorEnum::eFailed;
        }))
        .WillOnce(Invoke([&](SSL_CTX* context) {
            auto err = Configure(context);
            reloaded.set_value();
            return err;
        }));
    Notify();
    EXPECT_EQ(started.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_NO_THROW(EXPECT_EQ(Download(), "secure content"));
    Notify();
    release.set_value();
    EXPECT_EQ(latest.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(mServer->Stop().IsNone());
}

TEST_F(FileserverRotationTest, StopDuringLoadingWaitsAndDoesNotRestartListener)
{
    Init();
    ASSERT_TRUE(mServer->Start().IsNone());
    std::promise<void> loading;
    std::promise<void> release;
    auto               started = loading.get_future();
    auto               resume  = release.get_future();
    EXPECT_CALL(mConfigure, Call(_)).WillOnce(Invoke([&](SSL_CTX* context) {
        loading.set_value();
        resume.wait();
        return Configure(context);
    }));
    Notify();
    EXPECT_EQ(started.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto stopped = std::async(std::launch::async, [&]() { return mServer->Stop(); });
    EXPECT_EQ(stopped.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    release.set_value();
    EXPECT_TRUE(stopped.get().IsNone());
    EXPECT_ANY_THROW(Download());
}

TEST_F(FileserverRotationTest, StartWhileRunningKeepsExistingListener)
{
    Init();
    ASSERT_TRUE(mServer->Start().IsNone());
    EXPECT_TRUE(mServer->Start().Is(ErrorEnum::eWrongState));
    EXPECT_NO_THROW(EXPECT_EQ(Download(), "secure content"));
}

TEST_F(FileserverRotationTest, ContextExceptionIsReturnedAndStartupCanRetry)
{
    Init();
    EXPECT_CALL(mConfigure, Call(_))
        .WillOnce(Throw(std::runtime_error("cannot load credentials")))
        .WillOnce(Invoke([this](SSL_CTX* context) { return Configure(context); }));
    EXPECT_FALSE(mServer->Start().IsNone());
    EXPECT_ANY_THROW(Download());
    ASSERT_TRUE(mServer->Start().IsNone());
    EXPECT_NO_THROW(EXPECT_EQ(Download(), "secure content"));
}

TEST_F(FileserverRotationTest, OccupiedPortIsReturnedAndStartupCanRetry)
{
    Init();
    Poco::Net::ServerSocket occupied;
    occupied.bind(Poco::Net::SocketAddress("0.0.0.0", mPort), false, false);
    occupied.listen();
    EXPECT_FALSE(mServer->Start().IsNone());
    occupied.close();
    ASSERT_TRUE(mServer->Start().IsNone());
    EXPECT_NO_THROW(EXPECT_EQ(Download(), "secure content"));
}

TEST_F(FileserverRotationTest, StopInterruptsCertificateRetryDelay)
{
    Init(std::chrono::seconds(10));
    ASSERT_TRUE(mServer->Start().IsNone());
    std::promise<void> failed;
    auto               failure = failed.get_future();
    EXPECT_CALL(mConfigure, Call(_)).WillOnce(Invoke([&](SSL_CTX*) -> Error {
        failed.set_value();
        return ErrorEnum::eNotFound;
    }));
    Notify();
    EXPECT_EQ(failure.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto started = std::chrono::steady_clock::now();
    EXPECT_TRUE(mServer->Stop().IsNone());
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
    EXPECT_ANY_THROW(Download());
}

TEST_F(FileRequestHandlerTest, DownloadFileSuccess)
{
    std::string testContent = "This is a test file content for download";
    {
        std::ofstream testFile("download/test_file.txt");
        testFile << testContent;
    }

    Poco::Net::HTTPClientSession session("localhost", mPort);
    Poco::Net::HTTPRequest       request(Poco::Net::HTTPRequest::HTTP_GET, "/test_file.txt");
    Poco::Net::HTTPResponse      response;

    session.sendRequest(request);
    std::istream& rs = session.receiveResponse(response);

    EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);
    EXPECT_EQ(response.getContentType(), "text/plain");
    EXPECT_EQ(response.getContentLength(), testContent.length());
    EXPECT_FALSE(response.get("Last-Modified").empty());

    std::stringstream ss;
    Poco::StreamCopier::copyStream(rs, ss);
    EXPECT_EQ(ss.str(), testContent);

    std::filesystem::remove("download/test_file.txt");
}

TEST_F(FileRequestHandlerTest, DownloadFileNotFound)
{
    Poco::Net::HTTPClientSession session("localhost", mPort);
    Poco::Net::HTTPRequest       request(Poco::Net::HTTPRequest::HTTP_GET, "/non_existent_file.dat");
    Poco::Net::HTTPResponse      response;

    session.sendRequest(request);
    session.receiveResponse(response);

    EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
}

TEST_F(FileRequestHandlerTest, DownloadFileWithDifferentMimeTypes)
{
    {
        std::ofstream htmlFile("download/test.html");
        htmlFile << "<html><body>Test</body></html>";
    }

    {
        std::ofstream jsonFile("download/test.json");
        jsonFile << "{\"test\": \"value\"}";
    }

    // Test HTML file
    {
        Poco::Net::HTTPClientSession session("localhost", mPort);
        Poco::Net::HTTPRequest       request(Poco::Net::HTTPRequest::HTTP_GET, "/test.html");
        Poco::Net::HTTPResponse      response;

        session.sendRequest(request);
        auto& rs = session.receiveResponse(response);

        EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);
        EXPECT_EQ(response.getContentType(), "text/html");

        std::stringstream ss;
        Poco::StreamCopier::copyStream(rs, ss);
        EXPECT_EQ(ss.str(), "<html><body>Test</body></html>");
    }

    // Test JSON file
    {
        Poco::Net::HTTPClientSession session("localhost", mPort);
        Poco::Net::HTTPRequest       request(Poco::Net::HTTPRequest::HTTP_GET, "/test.json");
        Poco::Net::HTTPResponse      response;

        session.sendRequest(request);
        auto& rs = session.receiveResponse(response);

        EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);
        EXPECT_EQ(response.getContentType(), "application/json");

        std::stringstream ss;
        Poco::StreamCopier::copyStream(rs, ss);
        EXPECT_EQ(ss.str(), "{\"test\": \"value\"}");
    }
}

} // namespace aos::common::fileserver::test
