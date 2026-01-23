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
#include <Poco/StreamCopier.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tools/string.hpp>

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

        std::filesystem::create_directory("download");
        auto err = mFileserver.Init("http://localhost:8000", "download");
        EXPECT_EQ(err, ErrorEnum::eNone);

        err = mFileserver.Start();
        EXPECT_EQ(err, ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mFileserver.Stop();
        std::filesystem::remove_all("download");
    }

    Fileserver mFileserver;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CommonFileserverTest, TranslateFilePathURL)
{
    StaticString<256> url;
    auto              err = mFileserver.TranslateFilePathURL("download/test_file.dat", url);
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(url, "http://localhost:8000/test_file.dat");
}

TEST_F(CommonFileserverTest, DownloadFileSuccess)
{
    std::string testContent = "This is a test file content for download";
    {
        std::ofstream testFile("download/test_file.txt");
        testFile << testContent;
    }

    Poco::Net::HTTPClientSession session("localhost", 8000);
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

TEST_F(CommonFileserverTest, DownloadFileNotFound)
{
    Poco::Net::HTTPClientSession session("localhost", 8000);
    Poco::Net::HTTPRequest       request(Poco::Net::HTTPRequest::HTTP_GET, "/non_existent_file.dat");
    Poco::Net::HTTPResponse      response;

    session.sendRequest(request);
    session.receiveResponse(response);

    EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
}

TEST_F(CommonFileserverTest, DownloadFileWithDifferentMimeTypes)
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
        Poco::Net::HTTPClientSession session("localhost", 8000);
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
        Poco::Net::HTTPClientSession session("localhost", 8000);
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
