/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <filesystem>

#include <Poco/Data/SQLite/Connector.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <filesystem>

#include "database.hpp"
#include "logger/logmodule.hpp"

using namespace Poco::Data::Keywords;

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

inline std::string Stringify(const Poco::JSON::Object& json)
{
    std::ostringstream oss;

    Poco::JSON::Stringifier::stringify(json, oss);

    return oss.str();
}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Database::Database()
{
    Poco::Data::SQLite::Connector::registerConnector();
}

aos::Error Database::Init(const std::string& dbPath, const std::string& migrationPath)
{
    if (mSession && mSession->isConnected()) {
        return aos::ErrorEnum::eNone;
    }

    try {
        auto dirPath = std::filesystem::path(dbPath).parent_path();
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }

        mSession = std::make_unique<Poco::Data::Session>("SQLite", dbPath);
        CreateTables();

        mMigration.emplace(*mSession, migrationPath);
        mMigration->MigrateToVersion(mVersion);
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to initialize database: " << e.what();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * certhandler::StorageItf implementation
 **********************************************************************************************************************/

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

            if (auto err = certsInfo.PushBack(certInfo); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
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
 * nodemanager::NodeInfoStorageItf implementation
 **********************************************************************************************************************/

aos::Error Database::SetNodeInfo(const aos::NodeInfo& info)
{
    try {
        Poco::JSON::Object pocoNodeInfo;
        const auto         nodeInfo = Stringify(ConvertNodeInfoToJSON(info));

        *mSession << "INSERT OR REPLACE INTO nodeinfo (id, info) VALUES (?, ?);", bind(info.mNodeID.CStr()),
            bind(nodeInfo), now;

    } catch (const Poco::Exception&) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error Database::GetNodeInfo(const aos::String& nodeId, aos::NodeInfo& nodeInfo) const
{
    try {
        Poco::Data::Statement       statement {*mSession};
        Poco::Nullable<std::string> pocoInfo;

        statement << "SELECT info FROM nodeinfo WHERE id = ?;", bind(nodeId.CStr()), into(pocoInfo);
        if (statement.execute() == 0) {
            return aos::ErrorEnum::eNotFound;
        }

        nodeInfo.mNodeID = nodeId;

        if (!pocoInfo.isNull()) {
            Poco::JSON::Parser parser;

            const auto ptr = parser.parse(pocoInfo.value()).extract<Poco::JSON::Object::Ptr>();
            if (ptr == nullptr) {
                return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
            }

            auto err = ConvertNodeInfoFromJSON(*ptr, nodeInfo);
            if (!err.IsNone()) {
                return err;
            }
        }

    } catch (const Poco::Exception&) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error Database::GetAllNodeIds(aos::Array<aos::StaticString<aos::cNodeIDLen>>& ids) const
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

        return aos::ErrorEnum::eNone;
    } catch (const Poco::Exception&) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }
}

aos::Error Database::RemoveNodeInfo(const aos::String& nodeId)
{
    try {
        *mSession << "DELETE FROM nodeinfo WHERE id = ?;", bind(nodeId.CStr()), now;
    } catch (const Poco::Exception&) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
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

    *mSession << "CREATE TABLE IF NOT EXISTS nodeinfo ("
                 "id TEXT NOT NULL,"
                 "info TEXT,"
                 "PRIMARY KEY (id));",
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

Poco::JSON::Object Database::ConvertNodeInfoToJSON(const aos::NodeInfo& nodeInfo)
{
    Poco::JSON::Object object;

    object.set("status", static_cast<int>(nodeInfo.mStatus.GetValue()));
    object.set("type", nodeInfo.mNodeType.CStr());
    object.set("name", nodeInfo.mName.CStr());
    object.set("osType", nodeInfo.mOSType.CStr());
    object.set("cpuInfo", ConvertCpuInfoToJSON(nodeInfo.mCPUs));
    object.set("partitions", ConvertPartitionInfoToJSON(nodeInfo.mPartitions));
    object.set("attrs", ConvertAttributesToJSON(nodeInfo.mAttrs));
    object.set("maxDMIPS", nodeInfo.mMaxDMIPS);
    object.set("totalRAM", nodeInfo.mTotalRAM);

    return object;
}

aos::Error Database::ConvertNodeInfoFromJSON(const Poco::JSON::Object& object, aos::NodeInfo& dst)
{
    dst.mStatus   = static_cast<aos::NodeStatusEnum>(object.getValue<int>("status"));
    dst.mNodeType = object.getValue<std::string>("type").c_str();
    dst.mName     = object.getValue<std::string>("name").c_str();
    dst.mOSType   = object.getValue<std::string>("osType").c_str();
    dst.mMaxDMIPS = object.getValue<uint64_t>("maxDMIPS");
    dst.mTotalRAM = object.getValue<size_t>("totalRAM");

    const auto cpuInfo = object.get("cpuInfo").extract<Poco::JSON::Array::Ptr>();
    if (cpuInfo == nullptr) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    auto err = ConvertCpuInfoFromJSON(*cpuInfo, dst.mCPUs);
    if (!err.IsNone()) {
        return err;
    }

    const auto partitions = object.get("partitions").extract<Poco::JSON::Array::Ptr>();
    if (partitions == nullptr) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    err = ConvertPartitionInfoFromJSON(*partitions, dst.mPartitions);
    if (!err.IsNone()) {
        return err;
    }

    const auto attributes = object.get("attrs").extract<Poco::JSON::Array::Ptr>();
    if (attributes == nullptr) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return ConvertAttributesFromJSON(*attributes, dst.mAttrs);
}

Poco::JSON::Array Database::ConvertCpuInfoToJSON(const aos::Array<aos::CPUInfo>& cpuInfo)
{
    Poco::JSON::Array dst;

    for (const auto& srcItem : cpuInfo) {
        Poco::JSON::Object pocoItem;

        pocoItem.set("modelName", srcItem.mModelName.CStr());
        pocoItem.set("numCores", srcItem.mNumCores);
        pocoItem.set("numThreads", srcItem.mNumThreads);
        pocoItem.set("arch", srcItem.mArch.CStr());
        pocoItem.set("archFamily", srcItem.mArchFamily.CStr());
        pocoItem.set("maxDMIPS", srcItem.mMaxDMIPS);

        dst.add(pocoItem);
    }

    return dst;
}

aos::Error Database::ConvertCpuInfoFromJSON(const Poco::JSON::Array& src, aos::Array<aos::CPUInfo>& dst)
{
    for (const auto& srcItem : src) {
        aos::CPUInfo dstItem;

        const auto cpuInfo = srcItem.extract<Poco::JSON::Object::Ptr>();
        if (cpuInfo == nullptr) {
            return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
        }

        dstItem.mModelName  = cpuInfo->getValue<std::string>("modelName").c_str();
        dstItem.mNumCores   = cpuInfo->getValue<size_t>("numCores");
        dstItem.mNumThreads = cpuInfo->getValue<size_t>("numThreads");
        dstItem.mArch       = cpuInfo->getValue<std::string>("arch").c_str();
        dstItem.mArchFamily = cpuInfo->getValue<std::string>("archFamily").c_str();
        dstItem.mMaxDMIPS   = cpuInfo->getValue<uint64_t>("maxDMIPS");

        auto err = dst.PushBack(dstItem);
        if (!err.IsNone()) {
            return err;
        }
    }

    return aos::ErrorEnum::eNone;
}

Poco::JSON::Array Database::ConvertPartitionInfoToJSON(const aos::Array<aos::PartitionInfo>& partitionInfo)
{
    Poco::JSON::Array dst;

    for (const auto& srcItem : partitionInfo) {
        Poco::JSON::Object pocoItem;
        Poco::JSON::Array  types;

        pocoItem.set("name", srcItem.mName.CStr());
        for (const auto& type : srcItem.mTypes) {
            types.add(type.CStr());
        }
        pocoItem.set("types", types);
        pocoItem.set("totalSize", srcItem.mTotalSize);
        pocoItem.set("path", srcItem.mPath.CStr());
        pocoItem.set("usedSize", srcItem.mUsedSize);

        dst.add(pocoItem);
    }

    return dst;
}

aos::Error Database::ConvertPartitionInfoFromJSON(const Poco::JSON::Array& src, aos::Array<aos::PartitionInfo>& dst)
{
    for (const auto& srcItem : src) {
        aos::PartitionInfo dstItem;

        const auto partitionInfo = srcItem.extract<Poco::JSON::Object::Ptr>();
        if (partitionInfo == nullptr) {
            return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
        }

        const auto types = partitionInfo->get("types").extract<Poco::JSON::Array::Ptr>();
        if (types == nullptr) {
            return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
        }

        for (const auto& type : *types) {
            auto err = dstItem.mTypes.PushBack(type.convert<std::string>().c_str());
            if (!err.IsNone()) {
                return err;
            }
        }

        dstItem.mName      = partitionInfo->getValue<std::string>("name").c_str();
        dstItem.mTotalSize = partitionInfo->getValue<size_t>("totalSize");
        dstItem.mPath      = partitionInfo->getValue<std::string>("path").c_str();
        dstItem.mUsedSize  = partitionInfo->getValue<size_t>("usedSize");

        auto err = dst.PushBack(dstItem);
        if (!err.IsNone()) {
            return err;
        }
    }

    return aos::ErrorEnum::eNone;
}

Poco::JSON::Array Database::ConvertAttributesToJSON(const aos::Array<aos::NodeAttribute>& attributes)
{
    Poco::JSON::Array dst;

    for (const auto& srcItem : attributes) {
        Poco::JSON::Object pocoItem;

        pocoItem.set("name", srcItem.mName.CStr());
        pocoItem.set("value", srcItem.mValue.CStr());

        dst.add(pocoItem);
    }

    return dst;
}

aos::Error Database::ConvertAttributesFromJSON(const Poco::JSON::Array& src, aos::Array<aos::NodeAttribute>& dst)
{
    for (const auto& srcItem : src) {
        aos::NodeAttribute dstItem;

        const auto attribute = srcItem.extract<Poco::JSON::Object::Ptr>();
        if (attribute == nullptr) {
            return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
        }

        dstItem.mName  = attribute->getValue<std::string>("name").c_str();
        dstItem.mValue = attribute->getValue<std::string>("value").c_str();

        auto err = dst.PushBack(dstItem);
        if (!err.IsNone()) {
            return err;
        }
    }

    return aos::ErrorEnum::eNone;
}
