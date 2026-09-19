/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/StreamCopier.h>

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

TEST_F(CommonFileserverTest, ExplicitHTTPSPreservesHostAndPort)
{
    ASSERT_TRUE(
        mFileserver.Init("https://main:8094", "download", "cm", "ca.pem", mCertProvider, mCertLoader, mCryptoProvider)
            .IsNone());
    StaticString<256> url;
    ASSERT_TRUE(mFileserver.TranslateFilePathURL("download/test_file.dat", url).IsNone());
    EXPECT_EQ(url, "https://main:8094/test_file.dat");
}

TEST_F(CommonFileserverTest, EmptyHostDefaultsToLocalhost)
{
    ASSERT_TRUE(
        mFileserver.Init("https://:8094", "download", "cm", "ca.pem", mCertProvider, mCertLoader, mCryptoProvider)
            .IsNone());
    StaticString<256> url;
    ASSERT_TRUE(mFileserver.TranslateFilePathURL("download/test_file.dat", url).IsNone());
    EXPECT_EQ(url, "https://localhost:8094/test_file.dat");
}

TEST_F(CommonFileserverTest, InvalidURLReturnsErrorAndInitCanRetry)
{
    EXPECT_FALSE(
        mFileserver.Init("https://[invalid", "download", "cm", "ca.pem", mCertProvider, mCertLoader, mCryptoProvider)
            .IsNone());
    ASSERT_TRUE(
        mFileserver.Init("localhost:8094", "download", "cm", "ca.pem", mCertProvider, mCertLoader, mCryptoProvider)
            .IsNone());
    StaticString<256> url;
    ASSERT_TRUE(mFileserver.TranslateFilePathURL("download/test_file.dat", url).IsNone());
    EXPECT_EQ(url, "https://localhost:8094/test_file.dat");
}

TEST_F(CommonFileserverTest, TranslateWithoutInitReturnsWrongState)
{
    Fileserver        server;
    StaticString<256> url;
    EXPECT_TRUE(server.TranslateFilePathURL("download/test_file.dat", url).Is(ErrorEnum::eWrongState));
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
