/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <filesystem>

#include <Poco/Data/SQLite/Connector.h>
#include <Poco/Path.h>

#include <core/common/tools/logger.hpp>

#include <common/cloudprotocol/unitstatus.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>

#include "database.hpp"

using namespace Poco::Data::Keywords;

namespace aos::iam::database {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Database::Database()
{
    Poco::Data::SQLite::Connector::registerConnector();
}

Error Database::Init(const config::DatabaseConfig& config)
{
    if (mSession && mSession->isConnected()) {
        return ErrorEnum::eNone;
    }

    try {
        auto dirPath = std::filesystem::path(config.mWorkingDir);
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }

        const auto dbPath = Poco::Path(config.mWorkingDir, cDBFileName);
        mSession          = std::make_unique<Poco::Data::Session>("SQLite", dbPath.toString());
        CreateTables();

        mDatabase.emplace(*mSession, config.mMigrationPath, config.mMergedMigrationPath);

        CreateMigrationData(config);
        mDatabase->MigrateToVersion(GetVersion());
        DropMigrationData();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * certhandler::StorageItf implementation
 **********************************************************************************************************************/

Error Database::AddCertInfo(const String& certType, const aos::CertInfo& certInfo)
{
    try {
        CertInfo dbCertInfo;

        FromAosCertInfo(certType, certInfo, dbCertInfo);

        *mSession
            << "INSERT INTO certificates (type, issuer, serial, certURL, keyURL, notAfter) VALUES (?, ?, ?, ?, ?, ?);",
            bind(dbCertInfo), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveCertInfo(const String& certType, const String& certURL)
{
    try {
        *mSession << "DELETE FROM certificates WHERE type = ? AND certURL = ?;", bind(certType.CStr()),
            bind(certURL.CStr()), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveAllCertsInfo(const String& certType)
{
    try {
        *mSession << "DELETE FROM certificates WHERE type = ?;", bind(certType.CStr()), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetCertInfo(const Array<uint8_t>& issuer, const Array<uint8_t>& serial, aos::CertInfo& cert)
{
    try {
        CertInfo              result;
        Poco::Data::Statement statement {*mSession};

        statement << "SELECT * FROM certificates WHERE issuer = ? AND serial = ?;",
            bind(Poco::Data::BLOB {issuer.Get(), issuer.Size()}), bind(Poco::Data::BLOB {serial.Get(), serial.Size()}),
            into(result);

        if (statement.execute() == 0) {
            return ErrorEnum::eNotFound;
        }

        ToAosCertInfo(result, cert);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetCertsInfo(const String& certType, Array<aos::CertInfo>& certsInfo)
{
    try {
        std::vector<CertInfo> result;

        *mSession << "SELECT * FROM certificates WHERE type = ?;", bind(certType.CStr()), into(result), now;

        for (const auto& cert : result) {
            aos::CertInfo certInfo {};

            ToAosCertInfo(cert, certInfo);

            if (auto err = certsInfo.PushBack(certInfo); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Database::~Database()
{
    if (mSession && mSession->isConnected()) {
        mSession->close();
    }

    Poco::Data::SQLite::Connector::unregisterConnector();
}

/***********************************************************************************************************************
 * nodemanager::NodeInfoStorageItf implementation
 **********************************************************************************************************************/

Error Database::SetNodeInfo(const NodeInfo& info)
{
    try {
        auto nodeInfoJSON = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        auto err = common::cloudprotocol::ToJSON(info, *nodeInfoJSON);
        AOS_ERROR_CHECK_AND_THROW(err, "can't serialize node info to JSON");

        const auto nodeInfo = common::utils::Stringify(nodeInfoJSON);

        *mSession << "INSERT OR REPLACE INTO nodeinfo (id, info) VALUES (?, ?);", bind(info.mNodeID.CStr()),
            bind(nodeInfo), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetNodeInfo(const String& nodeID, NodeInfo& nodeInfo) const
{
    try {
        Poco::Data::Statement       statement {*mSession};
        Poco::Nullable<std::string> pocoInfo;

        statement << "SELECT info FROM nodeinfo WHERE id = ?;", bind(nodeID.CStr()), into(pocoInfo);
        if (statement.execute() == 0) {
            return ErrorEnum::eNotFound;
        }

        nodeInfo.mNodeID = nodeID;

        if (!pocoInfo.isNull()) {
            auto result = common::utils::ParseJson(pocoInfo.value());

            if (!result.mError.IsNone()) {
                return AOS_ERROR_WRAP(result.mError);
            }

            auto object = common::utils::CaseInsensitiveObjectWrapper(result.mValue);

            if (auto err = common::cloudprotocol::FromJSON(object, nodeInfo); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetAllNodeIDs(Array<StaticString<cIDLen>>& ids) const
{
    try {
        Poco::Data::Statement    statement {*mSession};
        std::vector<std::string> storedIds;

        statement << "SELECT id FROM nodeinfo;", into(storedIds);
        statement.execute();
        ids.Clear();

        for (const auto& id : storedIds) {
            auto err = ids.PushBack(id.c_str());
            if (!err.IsNone()) {
                return err;
            }
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error Database::RemoveNodeInfo(const String& nodeID)
{
    try {
        *mSession << "DELETE FROM nodeinfo WHERE id = ?;", bind(nodeID.CStr()), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

int Database::GetVersion() const
{
    return cVersion;
}

void Database::CreateMigrationData(const config::DatabaseConfig& config)
{
    DropMigrationData();

    *mSession << "CREATE TABLE IF NOT EXISTS pins (path TEXT NOT NULL, value TEXT NOT NULL);", now;

    std::string           pin, path;
    Poco::Data::Statement insert(*mSession);

    insert << "INSERT INTO pins (path, value) VALUES(?, ?);", use(path), use(pin);

    for (const auto& [key, value] : config.mPathToPin) {
        path = key;
        pin  = value;

        insert.execute();
    }
}

void Database::DropMigrationData()
{
    *mSession << "DROP TABLE IF EXISTS pins;", now;
}

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

    *mSession << "CREATE TABLE IF NOT EXISTS nodeinfo ("
                 "id TEXT NOT NULL,"
                 "info TEXT,"
                 "PRIMARY KEY (id));",
        now;
}

void Database::FromAosCertInfo(const String& certType, const aos::CertInfo& certInfo, CertInfo& result)
{
    result.set<CertColumns::eType>(certType.CStr());
    result.set<CertColumns::eIssuer>(Poco::Data::BLOB {certInfo.mIssuer.Get(), certInfo.mIssuer.Size()});
    result.set<CertColumns::eSerial>(Poco::Data::BLOB {certInfo.mSerial.Get(), certInfo.mSerial.Size()});
    result.set<CertColumns::eCertURL>(certInfo.mCertURL.CStr());
    result.set<CertColumns::eKeyURL>(certInfo.mKeyURL.CStr());
    result.set<CertColumns::eNotAfter>(certInfo.mNotAfter.UnixNano());
}

void Database::ToAosCertInfo(const CertInfo& certInfo, aos::CertInfo& result)
{
    result.mIssuer = Array<uint8_t>(reinterpret_cast<const uint8_t*>(certInfo.get<CertColumns::eIssuer>().rawContent()),
        certInfo.get<CertColumns::eIssuer>().size());
    result.mSerial = Array<uint8_t>(reinterpret_cast<const uint8_t*>(certInfo.get<CertColumns::eSerial>().rawContent()),
        certInfo.get<CertColumns::eSerial>().size());

    result.mCertURL = certInfo.get<CertColumns::eCertURL>().c_str();
    result.mKeyURL  = certInfo.get<CertColumns::eKeyURL>().c_str();

    result.mNotAfter = Time::Unix(certInfo.get<CertColumns::eNotAfter>() / Time::cSeconds.Nanoseconds(),
        certInfo.get<CertColumns::eNotAfter>() % Time::cSeconds.Nanoseconds());
}

} // namespace aos::iam::database
