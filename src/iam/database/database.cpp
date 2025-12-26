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
#include <Poco/Path.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>

#include "database.hpp"

using namespace Poco::Data::Keywords;

namespace aos::iam::database {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

Poco::JSON::Object::Ptr ToJSON(const OSInfo& osInfo)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    object->set("os", osInfo.mOS.CStr());

    if (osInfo.mVersion.HasValue()) {
        object->set("version", osInfo.mVersion->CStr());
    }

    if (!osInfo.mFeatures.IsEmpty()) {
        object->set("features", common::utils::ToJsonArray(osInfo.mFeatures, [](const auto& feature) -> std::string {
            return feature.CStr();
        }));
    }

    return object;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, OSInfo& dst)
{
    if (auto err = dst.mOS.Assign(object.GetValue<std::string>("os").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto version = object.GetOptionalValue<std::string>("version"); version.has_value()) {
        dst.mVersion.EmplaceValue();

        if (auto err = dst.mVersion->Assign(version->c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    common::utils::ForEach(object, "feature", [&dst](const Poco::Dynamic::Var& value) {
        auto err = dst.mFeatures.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse feature");

        err = dst.mFeatures.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse feature");
    });

    return ErrorEnum::eNone;
}

Poco::JSON::Object::Ptr ToJSON(const ArchInfo& archInfo)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    object->set("architecture", archInfo.mArchitecture.CStr());

    if (archInfo.mVariant.HasValue()) {
        object->set("variant", archInfo.mVariant->CStr());
    }

    return object;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, ArchInfo& dst)
{
    if (auto err = dst.mArchitecture.Assign(object.GetValue<std::string>("architecture").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto variant = object.GetOptionalValue<std::string>("variant"); variant.has_value()) {
        dst.mVariant.EmplaceValue();

        if (auto err = dst.mVariant->Assign(variant->c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Poco::JSON::Object::Ptr ToJSON(const CPUInfo& cpuInfo)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    object->set("model", cpuInfo.mModelName.CStr());
    object->set("cores", cpuInfo.mNumCores);
    object->set("threads", cpuInfo.mNumThreads);
    object->set("archInfo", ToJSON(cpuInfo.mArchInfo));

    if (cpuInfo.mMaxDMIPS.HasValue()) {
        object->set("maxDMIPS", cpuInfo.mMaxDMIPS.GetValue());
    }

    return object;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, CPUInfo& dst)
{
    if (auto err = dst.mModelName.Assign(object.GetValue<std::string>("model").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    dst.mNumCores   = object.GetValue<size_t>("cores");
    dst.mNumThreads = object.GetValue<size_t>("threads");

    if (!object.Has("archInfo")) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "Can't parse ArchInfo"));
    }

    if (auto err = FromJSON(object.GetObject("archInfo"), dst.mArchInfo); !err.IsNone()) {
        return err;
    }

    if (auto maxDMIPS = object.GetOptionalValue<size_t>("maxDMIPS"); maxDMIPS.has_value()) {
        dst.mMaxDMIPS.SetValue(maxDMIPS.value());
    }

    return ErrorEnum::eNone;
}

Poco::JSON::Object::Ptr ToJSON(const PartitionInfo& partitionInfo)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    object->set("name", partitionInfo.mName.CStr());

    if (!partitionInfo.mTypes.IsEmpty()) {
        object->set("types", common::utils::ToJsonArray(partitionInfo.mTypes, [](const auto& type) -> std::string {
            return type.CStr();
        }));
    }

    object->set("path", partitionInfo.mPath.CStr());
    object->set("totalSize", partitionInfo.mTotalSize);

    return object;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, PartitionInfo& dst)
{
    if (auto err = dst.mName.Assign(object.GetValue<std::string>("name").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    common::utils::ForEach(object, "types", [&dst](const Poco::Dynamic::Var& value) {
        auto err = dst.mTypes.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse partition type");

        err = dst.mTypes.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse partition type");
    });

    if (auto err = dst.mPath.Assign(object.GetValue<std::string>("path").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    dst.mTotalSize = object.GetValue<size_t>("totalSize");

    return ErrorEnum::eNone;
}

Poco::JSON::Object::Ptr ToJSON(const NodeAttribute& attr)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    object->set("name", attr.mName.CStr());
    object->set("value", attr.mValue.CStr());

    return object;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, NodeAttribute& dst)
{
    if (auto err = dst.mName.Assign(object.GetValue<std::string>("name").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mValue.Assign(object.GetValue<std::string>("value").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Poco::JSON::Object::Ptr ToJSON(const Error& error)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    object->set("aosCode", static_cast<int>(error.Value()));
    object->set("exitCode", error.Errno());
    object->set("message", error.Message());

    return object;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, Error& dst)
{
    dst = Error(
        static_cast<Error::Enum>(object.GetValue<int>("aosCode")), object.GetValue<std::string>("message").c_str());

    return ErrorEnum::eNone;
}

Poco::JSON::Object::Ptr ToJSON(const NodeInfo& nodeInfo)
{
    auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    object->set("id", nodeInfo.mNodeID.CStr());
    object->set("type", nodeInfo.mNodeType.CStr());
    object->set("title", nodeInfo.mTitle.CStr());
    object->set("maxDMIPS", nodeInfo.mMaxDMIPS);
    object->set("totalRAM", nodeInfo.mTotalRAM);

    if (nodeInfo.mPhysicalRAM.HasValue()) {
        object->set("physicalRAM", nodeInfo.mPhysicalRAM.GetValue());
    }

    object->set("osInfo", ToJSON(nodeInfo.mOSInfo));
    object->set(
        "cpus", common::utils::ToJsonArray(nodeInfo.mCPUs, [](const auto& cpuInfo) { return ToJSON(cpuInfo); }));

    if (!nodeInfo.mPartitions.IsEmpty()) {
        object->set("partitions", common::utils::ToJsonArray(nodeInfo.mPartitions, [](const auto& partitionInfo) {
            return ToJSON(partitionInfo);
        }));
    }

    if (!nodeInfo.mAttrs.IsEmpty()) {
        object->set(
            "attrs", common::utils::ToJsonArray(nodeInfo.mAttrs, [](const auto& attr) { return ToJSON(attr); }));
    }

    object->set("state", nodeInfo.mState.ToString().CStr());
    object->set("isConnected", nodeInfo.mIsConnected);

    if (!nodeInfo.mError.IsNone()) {
        object->set("error", ToJSON(nodeInfo.mError));
    }

    return object;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& object, NodeInfo& dst)
{
    if (auto err = dst.mNodeID.Assign(object.GetValue<std::string>("id").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mNodeType.Assign(object.GetValue<std::string>("type").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mTitle.Assign(object.GetValue<std::string>("title").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    dst.mMaxDMIPS = object.GetValue<size_t>("maxDMIPS");
    dst.mTotalRAM = object.GetValue<size_t>("totalRAM");

    if (auto physicalRAM = object.GetOptionalValue<size_t>("physicalRAM"); physicalRAM.has_value()) {
        dst.mPhysicalRAM.SetValue(physicalRAM.value());
    }

    if (!object.Has("osInfo")) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "Can't parse OSInfo"));
    }

    if (auto err = FromJSON(object.GetObject("osInfo"), dst.mOSInfo); !err.IsNone()) {
        return err;
    }

    common::utils::ForEach(object, "cpus", [&dst](const Poco::Dynamic::Var& value) {
        auto err = dst.mCPUs.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse CPU info");

        err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(value), dst.mCPUs.Back());
        AOS_ERROR_CHECK_AND_THROW(!err.IsNone(), "can't parse CPU info");
    });

    common::utils::ForEach(object, "partitions", [&dst](const Poco::Dynamic::Var& value) {
        auto err = dst.mPartitions.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse Partition info");

        err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(value), dst.mPartitions.Back());
        AOS_ERROR_CHECK_AND_THROW(!err.IsNone(), "can't parse Partition info");
    });

    common::utils::ForEach(object, "attrs", [&dst](const Poco::Dynamic::Var& value) {
        auto err = dst.mAttrs.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse Node attribute");

        err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(value), dst.mAttrs.Back());
        AOS_ERROR_CHECK_AND_THROW(!err.IsNone(), "can't parse Node attribute");
    });

    dst.mIsConnected = object.GetValue<bool>("isConnected");

    if (auto err = dst.mState.FromString(object.GetValue<std::string>("state").c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (object.Has("error")) {
        if (auto err = FromJSON(object.GetObject("error"), dst.mError); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

} // namespace

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
        Poco::JSON::Object pocoNodeInfo;
        const auto         nodeInfo = common::utils::Stringify(ToJSON(info));

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

            if (auto err = FromJSON(object, nodeInfo); !err.IsNone()) {
                return err;
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
