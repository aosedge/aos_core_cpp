/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <filesystem>

#include <Poco/Data/SQLite/Connector.h>

#include "database.hpp"
#include "log.hpp"

using namespace Poco::Data::Keywords;

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error Database::Init(const std::string& dbPath)
{
    if (mSession && mSession->isConnected()) {
        return aos::ErrorEnum::eNone;
    }

    try {
        auto dirPath = std::filesystem::path(dbPath).parent_path();
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }

        Poco::Data::SQLite::Connector::registerConnector();
        mSession = std::optional<Poco::Data::Session>(Poco::Data::Session("SQLite", dbPath));
        CreateTables();
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to initialize database: " << e.what();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error Database::AddCertInfo(const aos::String& certType, const aos::iam::certhandler::CertInfo& certInfo)
{
    try {
        *mSession
            << "INSERT INTO certificates (type, issuer, serial, certURL, keyURL, notAfter) VALUES (?, ?, ?, ?, ?, ?);",
            bind(ToAosCertInfo(certType, certInfo)), now;
    } catch (const Poco::Exception& e) {
        LOG_ERR() << "Failed to add certificate info: " << e.what();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error Database::RemoveCertInfo(const aos::String& certType, const aos::String& certURL)
{
    try {
        *mSession << "DELETE FROM certificates WHERE type = ? AND certURL = ?;", bind(certType.CStr()),
            bind(certURL.CStr()), now;
    } catch (const Poco::Exception& e) {
        LOG_ERR() << "Failed to remove certificate info: " << e.what();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error Database::RemoveAllCertsInfo(const aos::String& certType)
{
    try {
        *mSession << "DELETE FROM certificates WHERE type = ?;", bind(certType.CStr()), now;
    } catch (const Poco::Exception& e) {
        LOG_ERR() << "Failed to remove all certificate info: " << e.what();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error Database::GetCertInfo(
    const aos::Array<uint8_t>& issuer, const aos::Array<uint8_t>& serial, aos::iam::certhandler::CertInfo& cert)
{
    try {
        CertInfo              result;
        Poco::Data::Statement statement {*mSession};

        statement << "SELECT * FROM certificates WHERE issuer = ? AND serial = ?;",
            bind(Poco::Data::BLOB {issuer.Get(), issuer.Size()}), bind(Poco::Data::BLOB {serial.Get(), serial.Size()}),
            into(result);

        if (statement.execute() == 0) {
            return aos::ErrorEnum::eNotFound;
        }

        FromAosCertInfo(result, cert);
    } catch (const Poco::Exception& e) {
        LOG_ERR() << "Failed to get certificate info: " << e.what();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error Database::GetCertsInfo(const aos::String& certType, aos::Array<aos::iam::certhandler::CertInfo>& certsInfo)
{
    try {
        std::vector<CertInfo> result;

        *mSession << "SELECT * FROM certificates WHERE type = ?;", bind(certType.CStr()), into(result), now;

        for (const auto& cert : result) {
            aos::iam::certhandler::CertInfo certInfo {};

            FromAosCertInfo(cert, certInfo);
            certsInfo.PushBack(certInfo);
        }
    } catch (const Poco::Exception& e) {
        LOG_ERR() << "Failed to get certificates info: " << e.what();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

Database::~Database()
{
    if (mSession && mSession->isConnected()) {
        mSession->close();
    }

    Poco::Data::SQLite::Connector::unregisterConnector();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Database::CreateTables()
{
    *mSession << "CREATE TABLE IF NOT EXISTS certificates ("
                 "type TEXT NOT NULL,"
                 "issuer BLOB NOT NULL,"
                 "serial BLOB NOT NULL,"
                 "certURL TEXT,"
                 "keyURL TEXT,"
                 "notAfter TIMESTAMP,"
                 "PRIMARY KEY (issuer, serial));",
        now;
}

Database::CertInfo Database::ToAosCertInfo(const aos::String& certType, const aos::iam::certhandler::CertInfo& certInfo)
{
    CertInfo result;

    result.set<CertColumns::eType>(certType.CStr());
    result.set<CertColumns::eIssuer>(Poco::Data::BLOB {certInfo.mIssuer.Get(), certInfo.mIssuer.Size()});
    result.set<CertColumns::eSerial>(Poco::Data::BLOB {certInfo.mSerial.Get(), certInfo.mSerial.Size()});
    result.set<CertColumns::eCertURL>(certInfo.mCertURL.CStr());
    result.set<CertColumns::eKeyURL>(certInfo.mKeyURL.CStr());
    result.set<CertColumns::eNotAfter>(certInfo.mNotAfter.UnixNano());

    return result;
}

void Database::FromAosCertInfo(const CertInfo& certInfo, aos::iam::certhandler::CertInfo& result)
{
    result.mIssuer
        = aos::Array<uint8_t>(reinterpret_cast<const uint8_t*>(certInfo.get<CertColumns::eIssuer>().rawContent()),
            certInfo.get<CertColumns::eIssuer>().size());
    result.mSerial
        = aos::Array<uint8_t>(reinterpret_cast<const uint8_t*>(certInfo.get<CertColumns::eSerial>().rawContent()),
            certInfo.get<CertColumns::eSerial>().size());

    result.mCertURL = certInfo.get<CertColumns::eCertURL>().c_str();
    result.mKeyURL  = certInfo.get<CertColumns::eKeyURL>().c_str();

    result.mNotAfter = aos::Time::Unix(certInfo.get<CertColumns::eNotAfter>() / aos::Time::cSeconds,
        certInfo.get<CertColumns::eNotAfter>() % aos::Time::cSeconds);
}
