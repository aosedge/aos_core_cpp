/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <Poco/Data/SQLite/Connector.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Path.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>

#include "database.hpp"

using namespace Poco::Data::Keywords;

namespace aos::cm::database {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

template <typename E>
constexpr int ToInt(E e)
{
    return static_cast<int>(e);
}

std::string SerializeExposedPorts(const Array<networkmanager::ExposedPort>& ports)
{
    Poco::JSON::Array portsJson;

    for (const auto& port : ports) {
        Poco::JSON::Object portObj;

        portObj.set("protocol", port.mProtocol.CStr());
        portObj.set("port", port.mPort.CStr());
        portsJson.add(portObj);
    }

    return common::utils::Stringify(portsJson);
}

void DeserializeExposedPorts(const std::string& jsonStr, Array<networkmanager::ExposedPort>& ports)
{
    Poco::JSON::Parser parser;

    auto portsJson = parser.parse(jsonStr).extract<Poco::JSON::Array::Ptr>();
    if (portsJson == nullptr) {
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse exposed ports array");
    }

    ports.Clear();

    for (const auto& portJson : *portsJson) {
        const auto portObj = portJson.extract<Poco::JSON::Object::Ptr>();
        if (portObj == nullptr) {
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse exposed port");
        }

        networkmanager::ExposedPort port;

        AOS_ERROR_CHECK_AND_THROW(port.mProtocol.Assign(portObj->getValue<std::string>("protocol").c_str()));
        AOS_ERROR_CHECK_AND_THROW(port.mPort.Assign(portObj->getValue<std::string>("port").c_str()));
        AOS_ERROR_CHECK_AND_THROW(ports.PushBack(port), "can't add exposed port");
    }
}

std::string SerializeDNSServers(const Array<StaticString<cIPLen>>& dnsServers)
{
    Poco::JSON::Array dnsJSON;

    for (const auto& server : dnsServers) {
        dnsJSON.add(server.CStr());
    }

    return common::utils::Stringify(dnsJSON);
}

void DeserializeDNSServers(const std::string& jsonStr, Array<StaticString<cIPLen>>& dnsServers)
{
    Poco::JSON::Parser parser;

    auto dnsJSON = parser.parse(jsonStr).extract<Poco::JSON::Array::Ptr>();
    if (dnsJSON == nullptr) {
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse DNS servers array");
    }

    dnsServers.Clear();

    for (const auto& dnsJson : *dnsJSON) {
        AOS_ERROR_CHECK_AND_THROW(
            dnsServers.EmplaceBack(dnsJson.convert<std::string>().c_str()), "can't add DNS server");
    }
}

std::string SerializeLabels(const aos::LabelsArray& labels)
{
    Poco::JSON::Array labelsJSON;

    for (const auto& label : labels) {
        labelsJSON.add(label.CStr());
    }

    return common::utils::Stringify(labelsJSON);
}

void DeserializeLabels(const std::string& jsonStr, aos::LabelsArray& labels)
{
    Poco::JSON::Parser parser;

    auto labelsJSON = parser.parse(jsonStr).extract<Poco::JSON::Array::Ptr>();
    if (labelsJSON == nullptr) {
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse labels array");
    }

    labels.Clear();

    for (const auto& labelJson : *labelsJSON) {
        AOS_ERROR_CHECK_AND_THROW(labels.EmplaceBack(labelJson.convert<std::string>().c_str()), "can't add label");
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Database::Database()
{
    Poco::Data::SQLite::Connector::registerConnector();
}

Database::~Database()
{
    if (mSession && mSession->isConnected()) {
        mSession->close();
    }

    Poco::Data::SQLite::Connector::unregisterConnector();
}

Error Database::Init(const Config& config)
{
    std::lock_guard lock {mMutex};

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

        // Enable foreign key
        *mSession << "PRAGMA foreign_keys = ON;", now;

        CreateTables();

        mDatabase.emplace(*mSession, config.mMigrationPath, config.mMergedMigrationPath);

        mDatabase->MigrateToVersion(GetVersion());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * storagestate::StorageItf implementation
 **********************************************************************************************************************/

Error Database::AddStorageStateInfo(const storagestate::InstanceInfo& info)
{
    std::lock_guard lock {mMutex};

    try {
        StorageStateInstanceInfoRow row;

        FromAos(info, row);
        *mSession << "INSERT INTO storagestate (itemID, subjectID, instance, type,  preinstalled, storageQuota, "
                     "stateQuota, stateChecksum) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
            bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveStorageStateInfo(const InstanceIdent& instanceIdent)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM storagestate "
                     "WHERE itemID = ? AND subjectID = ? AND instance = ? AND type = ? AND preinstalled = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance),
            bind(instanceIdent.mType.ToString().CStr()), bind(instanceIdent.mPreinstalled);

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetAllStorageStateInfo(Array<storagestate::InstanceInfo>& info)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<StorageStateInstanceInfoRow> rows;
        *mSession << "SELECT itemID, subjectID, instance, type, preinstalled, storageQuota, stateQuota, stateChecksum "
                     "FROM storagestate;",
            into(rows), now;

        auto instanceInfo = std::make_unique<storagestate::InstanceInfo>();
        info.Clear();

        for (const auto& row : rows) {
            ToAos(row, *instanceInfo);
            AOS_ERROR_CHECK_AND_THROW(info.PushBack(*instanceInfo), "can't add storage state info");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetStorageStateInfo(const InstanceIdent& instanceIdent, storagestate::InstanceInfo& info)
{
    std::lock_guard lock {mMutex};

    try {
        StorageStateInstanceInfoRow row;
        Poco::Data::Statement       statement {*mSession};

        statement << "SELECT itemID, subjectID, instance, type, preinstalled, storageQuota, stateQuota, stateChecksum "
                     "FROM storagestate "
                     "WHERE itemID = ? AND subjectID = ? AND instance = ? AND type = ? AND preinstalled = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance),
            bind(instanceIdent.mType.ToString().CStr()), bind(instanceIdent.mPreinstalled), into(row);

        if (statement.execute() == 0) {
            return ErrorEnum::eNotFound;
        }

        ToAos(row, info);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::UpdateStorageStateInfo(const storagestate::InstanceInfo& info)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};
        Poco::Data::BLOB      checksumBlob(info.mStateChecksum.begin(), info.mStateChecksum.Size());

        statement << "UPDATE storagestate SET storageQuota = ?, stateQuota = ?, stateChecksum = ? WHERE "
                     "itemID = ? AND subjectID = ? AND instance = ? AND type = ? AND preinstalled = ?;",
            bind(info.mStorageQuota), bind(info.mStateQuota), bind(checksumBlob),
            bind(info.mInstanceIdent.mItemID.CStr()), bind(info.mInstanceIdent.mSubjectID.CStr()),
            bind(info.mInstanceIdent.mInstance), bind(info.mInstanceIdent.mType.ToString().CStr()),
            bind(info.mInstanceIdent.mPreinstalled);

        if (statement.execute() == 0) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * networkmanager::StorageItf implementation
 **********************************************************************************************************************/

Error Database::AddNetwork(const networkmanager::Network& network)
{
    std::lock_guard lock {mMutex};

    try {
        NetworkManagerNetworkRow row;

        FromAos(network, row);
        *mSession << "INSERT INTO networks (networkID, subnet, vlanID) VALUES (?, ?, ?);", bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::AddHost(const String& networkID, const networkmanager::Host& host)
{
    std::lock_guard lock {mMutex};

    try {
        NetworkManagerHostRow row;

        FromAos(networkID, host, row);
        *mSession << "INSERT INTO hosts (networkID, nodeID, ip) VALUES (?, ?, ?);", bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::AddInstance(const networkmanager::Instance& instance)
{
    std::lock_guard lock {mMutex};

    try {
        NetworkManagerInstanceRow row;

        FromAos(instance, row);
        *mSession << "INSERT INTO networkmanager_instances (itemID, subjectID, instance, type, preinstalled, "
                     "networkID, nodeID, ip, exposedPorts, dnsServers) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetNetworks(Array<networkmanager::Network>& networks)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<NetworkManagerNetworkRow> rows;

        *mSession << "SELECT networkID, subnet, vlanID FROM networks;", into(rows), now;

        auto network = std::make_unique<networkmanager::Network>();

        networks.Clear();

        for (const auto& row : rows) {
            ToAos(row, *network);
            AOS_ERROR_CHECK_AND_THROW(networks.PushBack(*network), "can't add network");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetHosts(const String& networkID, Array<networkmanager::Host>& hosts)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<NetworkManagerHostRow> rows;

        *mSession << "SELECT networkID, nodeID, ip FROM hosts WHERE networkID = ?;", bind(networkID.CStr()), into(rows),
            now;

        auto host = std::make_unique<networkmanager::Host>();

        hosts.Clear();

        for (const auto& row : rows) {
            ToAos(row, *host);
            AOS_ERROR_CHECK_AND_THROW(hosts.PushBack(*host), "can't add host");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetInstances(const String& networkID, const String& nodeID, Array<networkmanager::Instance>& instances)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<NetworkManagerInstanceRow> rows;

        *mSession << "SELECT itemID, subjectID, instance, type, preinstalled, networkID, nodeID, ip, exposedPorts, "
                     "dnsServers FROM networkmanager_instances WHERE networkID = ? AND nodeID = ?;",
            bind(networkID.CStr()), bind(nodeID.CStr()), into(rows), now;

        auto instance = std::make_unique<networkmanager::Instance>();

        instances.Clear();

        for (const auto& row : rows) {
            ToAos(row, *instance);
            AOS_ERROR_CHECK_AND_THROW(instances.PushBack(*instance), "can't add instance");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveNetwork(const String& networkID)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM networks WHERE networkID = ?;", bind(networkID.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveHost(const String& networkID, const String& nodeID)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM hosts WHERE networkID = ? AND nodeID = ?;", bind(networkID.CStr()),
            bind(nodeID.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveNetworkInstance(const InstanceIdent& instanceIdent)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM networkmanager_instances "
                     "WHERE itemID = ? AND subjectID = ? AND instance = ? AND type = ? AND preinstalled = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance),
            bind(instanceIdent.mType.ToString().CStr()), bind(instanceIdent.mPreinstalled);

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * launcher::StorageItf implementation
 **********************************************************************************************************************/

Error Database::AddInstance(const launcher::InstanceInfo& info)
{
    std::lock_guard lock {mMutex};

    try {
        LauncherInstanceInfoRow row;

        FromAos(info, row);
        *mSession << "INSERT INTO launcher_instances (itemID, subjectID, instance, type, preinstalled, manifestDigest, "
                     "nodeID, prevNodeID, runtimeID, uid, gid, timestamp, state, isUnitSubject, version, ownerID, "
                     "subjectType, labels, priority) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::UpdateInstance(const launcher::InstanceInfo& info)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement
            << "UPDATE launcher_instances SET manifestDigest = ?, nodeID = ?, prevNodeID = ?, runtimeID = ?, uid = ?, "
               "gid = ?, timestamp = ?, state = ?, isUnitSubject = ?, ownerID = ?, subjectType = ?, labels = ?, "
               "priority = ? "
               "WHERE itemID = ? AND subjectID = ? AND instance = ? "
               "AND type = ? AND preinstalled = ? AND version = ?;",
            bind(info.mManifestDigest.CStr()), bind(info.mNodeID.CStr()), bind(info.mPrevNodeID.CStr()),
            bind(info.mRuntimeID.CStr()), bind(info.mUID), bind(info.mGID), bind(info.mTimestamp.UnixNano()),
            bind(info.mState.ToString().CStr()), bind(info.mIsUnitSubject), bind(info.mOwnerID.CStr()),
            bind(info.mSubjectType.ToString().CStr()), bind(SerializeLabels(info.mLabels)), bind(info.mPriority),
            bind(info.mInstanceIdent.mItemID.CStr()), bind(info.mInstanceIdent.mSubjectID.CStr()),
            bind(info.mInstanceIdent.mInstance), bind(info.mInstanceIdent.mType.ToString().CStr()),
            bind(info.mInstanceIdent.mPreinstalled), bind(info.mVersion.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetInstance(const InstanceIdent& instanceID, launcher::InstanceInfo& info) const
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<LauncherInstanceInfoRow> rows;

        *mSession << "SELECT itemID, subjectID, instance, type, preinstalled, manifestDigest, nodeID, prevNodeID, "
                     "runtimeID, uid, gid, timestamp, state, isUnitSubject, version, ownerID, subjectType, labels, "
                     "priority "
                     "FROM launcher_instances WHERE itemID = ? AND subjectID = ? AND instance = ? "
                     "AND type = ? AND preinstalled = ?;",
            bind(instanceID.mItemID.CStr()), bind(instanceID.mSubjectID.CStr()), bind(instanceID.mInstance),
            bind(instanceID.mType.ToString().CStr()), bind(instanceID.mPreinstalled), into(rows), now;

        if (rows.size() != 1) {
            return ErrorEnum::eNotFound;
        }

        ToAos(rows[0], info);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetActiveInstances(Array<launcher::InstanceInfo>& instances) const
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<LauncherInstanceInfoRow> rows;

        *mSession << "SELECT itemID, subjectID, instance, type, preinstalled, manifestDigest, nodeID, prevNodeID, "
                     "runtimeID, uid, gid, timestamp, state, isUnitSubject, version, ownerID, subjectType, labels, "
                     "priority "
                     "FROM launcher_instances;",
            into(rows), now;

        auto instanceInfo = std::make_unique<launcher::InstanceInfo>();
        instances.Clear();

        for (const auto& row : rows) {
            ToAos(row, *instanceInfo);
            AOS_ERROR_CHECK_AND_THROW(instances.PushBack(*instanceInfo), "can't add instance");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveInstance(const InstanceIdent& instanceIdent)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement.reset(*mSession);
        statement << "DELETE FROM launcher_instances "
                     "WHERE itemID = ? AND subjectID = ? AND instance = ? AND type = ? AND preinstalled = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance),
            bind(instanceIdent.mType.ToString().CStr()), bind(instanceIdent.mPreinstalled);

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * imagemanager::StorageItf implementation
 **********************************************************************************************************************/

Error Database::AddItem(const imagemanager::ItemInfo& item)
{
    std::lock_guard lock {mMutex};

    try {
        ImageManagerItemInfoRow row;

        FromAos(item, row);
        *mSession
            << "INSERT INTO imagemanager (itemID, version, indexDigest, state, timestamp) VALUES (?, ?, ?, ?, ?);",
            bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveItem(const String& id, const String& version)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM imagemanager WHERE itemID = ? AND version = ?;", bind(id.CStr()),
            bind(version.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::UpdateItemState(const String& id, const String& version, ItemState state, Time timestamp)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "UPDATE imagemanager SET state = ?, timestamp = ? WHERE itemID = ? AND version = ?;",
            bind(static_cast<int>(state)), bind(timestamp.UnixNano()), bind(id.CStr()), bind(version.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetAllItemsInfos(Array<imagemanager::ItemInfo>& items)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<ImageManagerItemInfoRow> rows;

        *mSession << "SELECT itemID, version, indexDigest, state, timestamp FROM imagemanager;", into(rows), now;

        auto itemInfo = std::make_unique<imagemanager::ItemInfo>();

        items.Clear();

        for (const auto& row : rows) {
            ToAos(row, *itemInfo);
            AOS_ERROR_CHECK_AND_THROW(items.PushBack(*itemInfo), "can't add item info");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetItemInfos(const String& id, Array<imagemanager::ItemInfo>& items)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<ImageManagerItemInfoRow> rows;

        *mSession << "SELECT itemID, version, indexDigest, state, timestamp FROM imagemanager WHERE itemID = ?;",
            bind(id.CStr()), into(rows), now;

        auto itemInfo = std::make_unique<imagemanager::ItemInfo>();

        items.Clear();

        for (const auto& row : rows) {
            ToAos(row, *itemInfo);
            AOS_ERROR_CHECK_AND_THROW(items.PushBack(*itemInfo), "can't add item info");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Database::CreateTables()
{
    LOG_INF() << "Create storagestate table";

    *mSession << "CREATE TABLE IF NOT EXISTS storagestate ("
                 "itemID TEXT,"
                 "subjectID TEXT,"
                 "instance INTEGER,"
                 "type TEXT,"
                 "preinstalled INTEGER,"
                 "storageQuota INTEGER,"
                 "stateQuota INTEGER,"
                 "stateChecksum BLOB,"
                 "PRIMARY KEY(itemID,subjectID,instance,type,preinstalled)"
                 ");",
        now;

    LOG_INF() << "Create imagemanager table";

    *mSession << "CREATE TABLE IF NOT EXISTS imagemanager ("
                 "itemID TEXT,"
                 "version TEXT,"
                 "indexDigest TEXT,"
                 "state INTEGER,"
                 "timestamp INTEGER,"
                 "PRIMARY KEY(itemID,version)"
                 ");",
        now;

    LOG_INF() << "Create networks table";

    *mSession << "CREATE TABLE IF NOT EXISTS networks ("
                 "networkID TEXT,"
                 "subnet TEXT,"
                 "vlanID INTEGER,"
                 "PRIMARY KEY(networkID)"
                 ");",
        now;

    LOG_INF() << "Create hosts table";

    *mSession << "CREATE TABLE IF NOT EXISTS hosts ("
                 "networkID TEXT,"
                 "nodeID TEXT,"
                 "ip TEXT,"
                 "PRIMARY KEY(networkID,nodeID),"
                 "FOREIGN KEY(networkID) REFERENCES networks(networkID)"
                 ");",
        now;

    LOG_INF() << "Create networkmanager instances table";

    *mSession << "CREATE TABLE IF NOT EXISTS networkmanager_instances ("
                 "itemID TEXT,"
                 "subjectID TEXT,"
                 "instance INTEGER,"
                 "type TEXT,"
                 "preinstalled INTEGER,"
                 "networkID TEXT,"
                 "nodeID TEXT,"
                 "ip TEXT,"
                 "exposedPorts TEXT,"
                 "dnsServers TEXT,"
                 "PRIMARY KEY(itemID,subjectID,instance,type,preinstalled),"
                 "FOREIGN KEY(networkID) REFERENCES networks(networkID),"
                 "FOREIGN KEY(networkID,nodeID) REFERENCES hosts(networkID,nodeID)"
                 ");",
        now;

    LOG_INF() << "Create launcher instances table";

    *mSession << "CREATE TABLE IF NOT EXISTS launcher_instances ("
                 "itemID TEXT,"
                 "subjectID TEXT,"
                 "instance INTEGER,"
                 "type TEXT,"
                 "preinstalled INTEGER,"
                 "manifestDigest TEXT,"
                 "nodeID TEXT,"
                 "prevNodeID TEXT,"
                 "runtimeID TEXT,"
                 "uid INTEGER,"
                 "gid INTEGER,"
                 "timestamp INTEGER,"
                 "state TEXT,"
                 "isUnitSubject INTEGER,"
                 "version TEXT,"
                 "ownerID TEXT,"
                 "subjectType TEXT,"
                 "labels TEXT,"
                 "priority INTEGER,"
                 "PRIMARY KEY(itemID,subjectID,instance,type,preinstalled,version)"
                 ");",
        now;
}

int Database::GetVersion() const
{
    return cVersion;
}

void Database::FromAos(const storagestate::InstanceInfo& src, StorageStateInstanceInfoRow& dst)
{
    dst.set<ToInt(StorageStateInstanceInfoColumns::eItemID)>(src.mInstanceIdent.mItemID.CStr());
    dst.set<ToInt(StorageStateInstanceInfoColumns::eSubjectID)>(src.mInstanceIdent.mSubjectID.CStr());
    dst.set<ToInt(StorageStateInstanceInfoColumns::eInstance)>(src.mInstanceIdent.mInstance);
    dst.set<ToInt(StorageStateInstanceInfoColumns::eType)>(src.mInstanceIdent.mType.ToString().CStr());
    dst.set<ToInt(StorageStateInstanceInfoColumns::ePreinstalled)>(src.mInstanceIdent.mPreinstalled);
    dst.set<ToInt(StorageStateInstanceInfoColumns::eStorageQuota)>(src.mStorageQuota);
    dst.set<ToInt(StorageStateInstanceInfoColumns::eStateQuota)>(src.mStateQuota);
    dst.set<ToInt(StorageStateInstanceInfoColumns::eStateChecksum)>(
        Poco::Data::BLOB(src.mStateChecksum.begin(), src.mStateChecksum.Size()));
}

void Database::ToAos(const StorageStateInstanceInfoRow& src, storagestate::InstanceInfo& dst)
{
    const auto& blob = src.get<ToInt(StorageStateInstanceInfoColumns::eStateChecksum)>();

    dst.mInstanceIdent.mItemID       = src.get<ToInt(StorageStateInstanceInfoColumns::eItemID)>().c_str();
    dst.mInstanceIdent.mSubjectID    = src.get<ToInt(StorageStateInstanceInfoColumns::eSubjectID)>().c_str();
    dst.mInstanceIdent.mInstance     = src.get<ToInt(StorageStateInstanceInfoColumns::eInstance)>();
    dst.mInstanceIdent.mPreinstalled = src.get<ToInt(StorageStateInstanceInfoColumns::ePreinstalled)>();
    dst.mStorageQuota                = src.get<ToInt(StorageStateInstanceInfoColumns::eStorageQuota)>();
    dst.mStateQuota                  = src.get<ToInt(StorageStateInstanceInfoColumns::eStateQuota)>();
    AOS_ERROR_CHECK_AND_THROW(
        dst.mInstanceIdent.mType.FromString(src.get<ToInt(StorageStateInstanceInfoColumns::eType)>().c_str()),
        "failed to parse instance type");
    AOS_ERROR_CHECK_AND_THROW(dst.mStateChecksum.Assign(Array<uint8_t>(blob.rawContent(), blob.size())));
}

void Database::FromAos(const networkmanager::Network& src, NetworkManagerNetworkRow& dst)
{
    dst.set<ToInt(NetworkManagerNetworkColumns::eNetworkID)>(src.mNetworkID.CStr());
    dst.set<ToInt(NetworkManagerNetworkColumns::eSubnet)>(src.mSubnet.CStr());
    dst.set<ToInt(NetworkManagerNetworkColumns::eVlanID)>(src.mVlanID);
}

void Database::ToAos(const NetworkManagerNetworkRow& src, networkmanager::Network& dst)
{
    dst.mNetworkID = src.get<ToInt(NetworkManagerNetworkColumns::eNetworkID)>().c_str();
    dst.mSubnet    = src.get<ToInt(NetworkManagerNetworkColumns::eSubnet)>().c_str();
    dst.mVlanID    = src.get<ToInt(NetworkManagerNetworkColumns::eVlanID)>();
}

void Database::FromAos(const String& networkID, const networkmanager::Host& src, NetworkManagerHostRow& dst)
{
    dst.set<ToInt(NetworkManagerHostColumns::eNetworkID)>(networkID.CStr());
    dst.set<ToInt(NetworkManagerHostColumns::eNodeID)>(src.mNodeID.CStr());
    dst.set<ToInt(NetworkManagerHostColumns::eIP)>(src.mIP.CStr());
}

void Database::ToAos(const NetworkManagerHostRow& src, networkmanager::Host& dst)
{
    dst.mNodeID = src.get<ToInt(NetworkManagerHostColumns::eNodeID)>().c_str();
    dst.mIP     = src.get<ToInt(NetworkManagerHostColumns::eIP)>().c_str();
}

void Database::FromAos(const networkmanager::Instance& src, NetworkManagerInstanceRow& dst)
{
    dst.set<ToInt(NetworkManagerInstanceColumns::eItemID)>(src.mInstanceIdent.mItemID.CStr());
    dst.set<ToInt(NetworkManagerInstanceColumns::eSubjectID)>(src.mInstanceIdent.mSubjectID.CStr());
    dst.set<ToInt(NetworkManagerInstanceColumns::eInstance)>(src.mInstanceIdent.mInstance);
    dst.set<ToInt(NetworkManagerInstanceColumns::eType)>(src.mInstanceIdent.mType.ToString().CStr());
    dst.set<ToInt(NetworkManagerInstanceColumns::ePreinstalled)>(src.mInstanceIdent.mPreinstalled);
    dst.set<ToInt(NetworkManagerInstanceColumns::eNetworkID)>(src.mNetworkID.CStr());
    dst.set<ToInt(NetworkManagerInstanceColumns::eNodeID)>(src.mNodeID.CStr());
    dst.set<ToInt(NetworkManagerInstanceColumns::eIP)>(src.mIP.CStr());
    dst.set<ToInt(NetworkManagerInstanceColumns::eExposedPorts)>(SerializeExposedPorts(src.mExposedPorts));
    dst.set<ToInt(NetworkManagerInstanceColumns::eDNSServers)>(SerializeDNSServers(src.mDNSServers));
}

void Database::ToAos(const NetworkManagerInstanceRow& src, networkmanager::Instance& dst)
{
    dst.mInstanceIdent.mItemID    = src.get<ToInt(NetworkManagerInstanceColumns::eItemID)>().c_str();
    dst.mInstanceIdent.mSubjectID = src.get<ToInt(NetworkManagerInstanceColumns::eSubjectID)>().c_str();
    dst.mInstanceIdent.mInstance  = src.get<ToInt(NetworkManagerInstanceColumns::eInstance)>();
    AOS_ERROR_CHECK_AND_THROW(
        dst.mInstanceIdent.mType.FromString(src.get<ToInt(NetworkManagerInstanceColumns::eType)>().c_str()),
        "failed to parse instance type");
    dst.mInstanceIdent.mPreinstalled = src.get<ToInt(NetworkManagerInstanceColumns::ePreinstalled)>();
    dst.mNetworkID                   = src.get<ToInt(NetworkManagerInstanceColumns::eNetworkID)>().c_str();
    dst.mNodeID                      = src.get<ToInt(NetworkManagerInstanceColumns::eNodeID)>().c_str();
    dst.mIP                          = src.get<ToInt(NetworkManagerInstanceColumns::eIP)>().c_str();

    DeserializeExposedPorts(src.get<ToInt(NetworkManagerInstanceColumns::eExposedPorts)>(), dst.mExposedPorts);
    DeserializeDNSServers(src.get<ToInt(NetworkManagerInstanceColumns::eDNSServers)>(), dst.mDNSServers);
}

void Database::FromAos(const launcher::InstanceInfo& src, LauncherInstanceInfoRow& dst)
{
    dst.set<ToInt(LauncherInstanceInfoColumns::eItemID)>(src.mInstanceIdent.mItemID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eSubjectID)>(src.mInstanceIdent.mSubjectID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eInstance)>(src.mInstanceIdent.mInstance);
    dst.set<ToInt(LauncherInstanceInfoColumns::eType)>(src.mInstanceIdent.mType.ToString().CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::ePreinstalled)>(src.mInstanceIdent.mPreinstalled);
    dst.set<ToInt(LauncherInstanceInfoColumns::eManifestDigest)>(src.mManifestDigest.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eNodeID)>(src.mNodeID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::ePrevNodeID)>(src.mPrevNodeID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eRuntimeID)>(src.mRuntimeID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eUID)>(src.mUID);
    dst.set<ToInt(LauncherInstanceInfoColumns::eGID)>(src.mGID);
    dst.set<ToInt(LauncherInstanceInfoColumns::eTimestamp)>(src.mTimestamp.UnixNano());
    dst.set<ToInt(LauncherInstanceInfoColumns::eState)>(src.mState.ToString().CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eIsUnitSubject)>(src.mIsUnitSubject);
    dst.set<ToInt(LauncherInstanceInfoColumns::eVersion)>(src.mVersion.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eOwnerID)>(src.mOwnerID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eSubjectType)>(src.mSubjectType.ToString().CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eLabels)>(SerializeLabels(src.mLabels));
    dst.set<ToInt(LauncherInstanceInfoColumns::ePriority)>(src.mPriority);
}

void Database::ToAos(const LauncherInstanceInfoRow& src, launcher::InstanceInfo& dst)
{
    dst.mInstanceIdent.mItemID    = src.get<ToInt(LauncherInstanceInfoColumns::eItemID)>().c_str();
    dst.mInstanceIdent.mSubjectID = src.get<ToInt(LauncherInstanceInfoColumns::eSubjectID)>().c_str();
    dst.mInstanceIdent.mInstance  = src.get<ToInt(LauncherInstanceInfoColumns::eInstance)>();
    AOS_ERROR_CHECK_AND_THROW(
        dst.mInstanceIdent.mType.FromString(src.get<ToInt(LauncherInstanceInfoColumns::eType)>().c_str()),
        "failed to parse instance type");
    dst.mInstanceIdent.mPreinstalled = src.get<ToInt(LauncherInstanceInfoColumns::ePreinstalled)>();
    dst.mManifestDigest              = src.get<ToInt(LauncherInstanceInfoColumns::eManifestDigest)>().c_str();
    dst.mNodeID                      = src.get<ToInt(LauncherInstanceInfoColumns::eNodeID)>().c_str();
    dst.mPrevNodeID                  = src.get<ToInt(LauncherInstanceInfoColumns::ePrevNodeID)>().c_str();
    dst.mRuntimeID                   = src.get<ToInt(LauncherInstanceInfoColumns::eRuntimeID)>().c_str();
    dst.mUID                         = src.get<ToInt(LauncherInstanceInfoColumns::eUID)>();
    dst.mGID                         = src.get<ToInt(LauncherInstanceInfoColumns::eGID)>();

    auto timestamp = src.get<ToInt(LauncherInstanceInfoColumns::eTimestamp)>();
    dst.mTimestamp = Time::Unix(timestamp / Time::cSeconds.Nanoseconds(), timestamp % Time::cSeconds.Nanoseconds());

    const auto& stateStr = src.get<ToInt(LauncherInstanceInfoColumns::eState)>();
    AOS_ERROR_CHECK_AND_THROW(dst.mState.FromString(stateStr.c_str()), "failed to parse instance state");

    dst.mIsUnitSubject = src.get<ToInt(LauncherInstanceInfoColumns::eIsUnitSubject)>();
    dst.mVersion       = src.get<ToInt(LauncherInstanceInfoColumns::eVersion)>().c_str();
    dst.mOwnerID       = src.get<ToInt(LauncherInstanceInfoColumns::eOwnerID)>().c_str();

    const auto& subjectTypeStr = src.get<ToInt(LauncherInstanceInfoColumns::eSubjectType)>();
    AOS_ERROR_CHECK_AND_THROW(dst.mSubjectType.FromString(subjectTypeStr.c_str()), "failed to parse subject type");

    DeserializeLabels(src.get<ToInt(LauncherInstanceInfoColumns::eLabels)>(), dst.mLabels);
    dst.mPriority = src.get<ToInt(LauncherInstanceInfoColumns::ePriority)>();
}

void Database::FromAos(const imagemanager::ItemInfo& src, ImageManagerItemInfoRow& dst)
{
    dst.set<ToInt(ImageManagerItemInfoColumns::eItemID)>(src.mItemID.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eVersion)>(src.mVersion.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eIndexDigest)>(src.mIndexDigest.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eState)>(static_cast<int>(src.mState));
    dst.set<ToInt(ImageManagerItemInfoColumns::eTimestamp)>(src.mTimestamp.UnixNano());
}

void Database::ToAos(const ImageManagerItemInfoRow& src, imagemanager::ItemInfo& dst)
{
    dst.mItemID      = src.get<ToInt(ImageManagerItemInfoColumns::eItemID)>().c_str();
    dst.mVersion     = src.get<ToInt(ImageManagerItemInfoColumns::eVersion)>().c_str();
    dst.mIndexDigest = src.get<ToInt(ImageManagerItemInfoColumns::eIndexDigest)>().c_str();
    dst.mState       = static_cast<ItemStateEnum>(src.get<ToInt(ImageManagerItemInfoColumns::eState)>());

    auto timestamp = src.get<ToInt(ImageManagerItemInfoColumns::eTimestamp)>();
    dst.mTimestamp = Time::Unix(timestamp / Time::cSeconds.Nanoseconds(), timestamp % Time::cSeconds.Nanoseconds());
}

} // namespace aos::cm::database
