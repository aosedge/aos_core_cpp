/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <Poco/Data/SQLite/Connector.h>
#include <Poco/Data/Transaction.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Path.h>

#include <core/common/tools/logger.hpp>

#include <common/cloudprotocol/desiredstatus.hpp>
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
    Poco::JSON::Array portsJSON;

    for (const auto& port : ports) {
        Poco::JSON::Object portObj;

        portObj.set("protocol", port.mProtocol.CStr());
        portObj.set("port", port.mPort.CStr());
        portsJSON.add(portObj);
    }

    return common::utils::Stringify(portsJSON);
}

void DeserializeExposedPorts(const std::string& jsonStr, Array<networkmanager::ExposedPort>& ports)
{
    Poco::JSON::Parser parser;

    auto portsJSON = parser.parse(jsonStr).extract<Poco::JSON::Array::Ptr>();
    if (portsJSON == nullptr) {
        AOS_ERROR_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse exposed ports array");
    }

    ports.Clear();

    for (const auto& portJSON : *portsJSON) {
        const auto portObj = portJSON.extract<Poco::JSON::Object::Ptr>();
        if (portObj == nullptr) {
            AOS_ERROR_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse exposed port");
        }

        networkmanager::ExposedPort port;

        auto err = port.mProtocol.Assign(portObj->getValue<std::string>("protocol").c_str());
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to assign protocol");

        err = port.mPort.Assign(portObj->getValue<std::string>("port").c_str());
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to assign port");

        err = ports.PushBack(port);
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add exposed port");
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

    auto dnsServersJSON = parser.parse(jsonStr).extract<Poco::JSON::Array::Ptr>();
    if (dnsServersJSON == nullptr) {
        AOS_ERROR_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse DNS servers array");
    }

    dnsServers.Clear();

    for (const auto& dnsJSON : *dnsServersJSON) {
        auto err = dnsServers.EmplaceBack(dnsJSON.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add DNS server");
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
        AOS_ERROR_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse labels array");
    }

    labels.Clear();

    for (const auto& labelJson : *labelsJSON) {
        auto err = labels.EmplaceBack(labelJson.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add label");
    }
}

std::string SerializeDesiredStatus(const DesiredStatus& desiredStatus)
{
    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = common::cloudprotocol::ToJSON(desiredStatus, *json);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to serialize desired status");

    return common::utils::Stringify(json);
}

void DeserializeDesiredStatus(const std::string& jsonStr, DesiredStatus& desiredStatus)
{
    auto [json, err] = common::utils::ParseJson(jsonStr);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse desired status JSON");

    err = common::cloudprotocol::FromJSON(common::utils::CaseInsensitiveObjectWrapper(json), desiredStatus);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to deserialize desired status");
}

std::string SerializeEnvVars(const EnvVarInfoArray& variables)
{
    auto varsArray = Poco::makeShared<Poco::JSON::Array>(Poco::JSON_PRESERVE_KEY_ORDER);

    for (const auto& var : variables) {
        auto varObj = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        varObj->set("name", var.mName.CStr());
        varObj->set("value", var.mValue.CStr());

        if (var.mTTL.HasValue()) {
            varObj->set("ttl", static_cast<int64_t>(var.mTTL.GetValue().UnixNano()));
        }

        varsArray->add(varObj);
    }

    return common::utils::Stringify(varsArray);
}

void DeserializeEnvVars(const std::string& jsonStr, EnvVarInfoArray& variables)
{
    Poco::JSON::Parser parser;

    auto varsArrayJSON = parser.parse(jsonStr).extract<Poco::JSON::Array::Ptr>();
    if (varsArrayJSON == nullptr) {
        AOS_ERROR_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse env vars array");
    }

    variables.Clear();

    for (const auto& varJSON : *varsArrayJSON) {
        const auto varObj = varJSON.extract<Poco::JSON::Object::Ptr>();
        if (varObj == nullptr) {
            AOS_ERROR_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse env var");
        }

        EnvVarInfo var;

        auto err = var.mName.Assign(varObj->getValue<std::string>("name").c_str());
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to assign env var name");

        err = var.mValue.Assign(varObj->getValue<std::string>("value").c_str());
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to assign env var value");

        if (varObj->has("ttl")) {
            int64_t ttlNano = varObj->getValue<int64_t>("ttl");
            var.mTTL.SetValue(
                Time::Unix(ttlNano / Time::cSeconds.Nanoseconds(), ttlNano % Time::cSeconds.Nanoseconds()));
        }

        err = variables.PushBack(var);
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add env var");
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
            auto err = info.PushBack(*instanceInfo);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add storage state info");
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
            auto err = networks.PushBack(*network);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add network");
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
            auto err = hosts.PushBack(*host);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add host");
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
            auto err = instances.PushBack(*instance);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add instance");
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

Error Database::AddPendingConnection(const networkmanager::PendingConnection& connection)
{
    std::lock_guard lock {mMutex};

    try {
        PendingConnectionRow row;

        FromAos(connection, row);
        *mSession << "INSERT INTO pending_connections (requesterItemID, requesterSubjectID, requesterInstance, "
                     "requesterType, requesterPreinstalled, nodeID, networkID, requesterIP, requesterSubnet, "
                     "targetItemID, port, protocol) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetPendingConnectionsByTarget(
    const String& targetItemID, Array<networkmanager::PendingConnection>& connections)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<PendingConnectionRow> rows;

        *mSession << "SELECT requesterItemID, requesterSubjectID, requesterInstance, requesterType, "
                     "requesterPreinstalled, nodeID, networkID, requesterIP, requesterSubnet, targetItemID, "
                     "port, protocol FROM pending_connections WHERE targetItemID = ?;",
            bind(targetItemID.CStr()), into(rows), now;

        auto connection = std::make_unique<networkmanager::PendingConnection>();

        connections.Clear();

        for (const auto& row : rows) {
            ToAos(row, *connection);

            auto err = connections.PushBack(*connection);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add pending connection");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetAllPendingConnections(Array<networkmanager::PendingConnection>& connections)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<PendingConnectionRow> rows;

        *mSession << "SELECT requesterItemID, requesterSubjectID, requesterInstance, requesterType, "
                     "requesterPreinstalled, nodeID, networkID, requesterIP, requesterSubnet, targetItemID, "
                     "port, protocol FROM pending_connections;",
            into(rows), now;

        auto connection = std::make_unique<networkmanager::PendingConnection>();

        connections.Clear();

        for (const auto& row : rows) {
            ToAos(row, *connection);

            auto err = connections.PushBack(*connection);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add pending connection");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemovePendingConnection(const networkmanager::PendingConnection& connection)
{
    std::lock_guard lock {mMutex};

    try {
        *mSession << "DELETE FROM pending_connections "
                     "WHERE requesterItemID = ? AND requesterSubjectID = ? AND requesterInstance = ? "
                     "AND requesterType = ? AND requesterPreinstalled = ? AND targetItemID = ? "
                     "AND port = ? AND protocol = ?;",
            bind(connection.mRequesterIdent.mItemID.CStr()), bind(connection.mRequesterIdent.mSubjectID.CStr()),
            bind(connection.mRequesterIdent.mInstance), bind(connection.mRequesterIdent.mType.ToString().CStr()),
            bind(connection.mRequesterIdent.mPreinstalled), bind(connection.mTargetItemID.CStr()),
            bind(connection.mPort.CStr()), bind(connection.mProtocol.CStr()), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemovePendingConnections(const InstanceIdent& requesterIdent)
{
    std::lock_guard lock {mMutex};

    try {
        *mSession << "DELETE FROM pending_connections "
                     "WHERE requesterItemID = ? AND requesterSubjectID = ? AND requesterInstance = ? "
                     "AND requesterType = ? AND requesterPreinstalled = ?;",
            bind(requesterIdent.mItemID.CStr()), bind(requesterIdent.mSubjectID.CStr()), bind(requesterIdent.mInstance),
            bind(requesterIdent.mType.ToString().CStr()), bind(requesterIdent.mPreinstalled), now;
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
                     "subjectType, labels, priority, disableRebalancing) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
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
               "priority = ?, disableRebalancing = ? "
               "WHERE itemID = ? AND subjectID = ? AND instance = ? "
               "AND type = ? AND preinstalled = ? AND version = ?;",
            bind(info.mManifestDigest.CStr()), bind(info.mNodeID.CStr()), bind(info.mPrevNodeID.CStr()),
            bind(info.mRuntimeID.CStr()), bind(info.mUID), bind(info.mGID), bind(info.mTimestamp.UnixNano()),
            bind(info.mState.ToString().CStr()), bind(info.mIsUnitSubject), bind(info.mOwnerID.CStr()),
            bind(info.mSubjectType.ToString().CStr()), bind(SerializeLabels(info.mLabels)), bind(info.mPriority),
            bind(info.mDisableRebalancing), bind(info.mInstanceIdent.mItemID.CStr()),
            bind(info.mInstanceIdent.mSubjectID.CStr()), bind(info.mInstanceIdent.mInstance),
            bind(info.mInstanceIdent.mType.ToString().CStr()), bind(info.mInstanceIdent.mPreinstalled),
            bind(info.mVersion.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::LoadActiveInstances(Array<launcher::InstanceInfo>& instances) const
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<LauncherInstanceInfoRow> rows;

        *mSession << "SELECT itemID, subjectID, instance, type, preinstalled, manifestDigest, nodeID, prevNodeID, "
                     "runtimeID, uid, gid, timestamp, state, isUnitSubject, version, ownerID, subjectType, labels, "
                     "priority, disableRebalancing "
                     "FROM launcher_instances;",
            into(rows), now;

        auto instanceInfo = std::make_unique<launcher::InstanceInfo>();
        instances.Clear();

        for (const auto& row : rows) {
            ToAos(row, *instanceInfo);
            auto err = instances.PushBack(*instanceInfo);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add instance");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveInstance(const InstanceIdent& instanceIdent, const String& version)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement.reset(*mSession);
        statement << "DELETE FROM launcher_instances "
                     "WHERE itemID = ? AND subjectID = ? AND instance = ? AND type = ? AND preinstalled = ? AND "
                     "version = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance),
            bind(instanceIdent.mType.ToString().CStr()), bind(instanceIdent.mPreinstalled), bind(version.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::SaveOverrideEnvVars(const OverrideEnvVarsRequest& envVars)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Transaction transaction(*mSession);

        *mSession << "DELETE FROM launcher_override_envvars;", now;

        for (const auto& item : envVars.mItems) {
            LauncherOverrideEnvVarsRow row;
            FromAos(item, row);
            *mSession << "INSERT INTO launcher_override_envvars (itemID, subjectID, instance, variables) "
                         "VALUES (?, ?, ?, ?);",
                bind(row), now;
        }
        transaction.commit();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::LoadOverrideEnvVars(OverrideEnvVarsRequest& envVars) const
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<LauncherOverrideEnvVarsRow> rows;

        *mSession << "SELECT itemID, subjectID, instance, variables FROM launcher_override_envvars;", into(rows), now;

        auto item = std::make_unique<EnvVarsInstanceInfo>();
        envVars.mCorrelationID.Clear();
        envVars.mItems.Clear();

        for (const auto& row : rows) {
            ToAos(row, *item);

            auto err = envVars.mItems.PushBack(*item);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add env vars item");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::LoadRunRequests(Array<launcher::RunInstanceRequest>& requests) const
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<LauncherRunRequestRow> rows;

        *mSession << "SELECT itemID, type, version, ownerID, subjectID, subjectType, isUnitSubject, priority, "
                     "numInstances, labels FROM launcher_run_requests ORDER BY rowid;",
            into(rows), now;

        requests.Clear();
        auto request = std::make_unique<launcher::RunInstanceRequest>();

        for (const auto& row : rows) {
            ToAos(row, *request);
            auto err = requests.PushBack(*request);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add run request");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::SaveRunRequests(const Array<launcher::RunInstanceRequest>& requests)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Transaction transaction(*mSession);

        *mSession << "DELETE FROM launcher_run_requests;", now;

        for (const auto& request : requests) {
            LauncherRunRequestRow row;

            FromAos(request, row);
            *mSession << "INSERT INTO launcher_run_requests (itemID, type, version, ownerID, subjectID, subjectType, "
                         "isUnitSubject, priority, numInstances, labels) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                bind(row), now;
        }
        transaction.commit();
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
        *mSession << "INSERT INTO imagemanager (itemID, type, version, indexDigest, state, timestamp) VALUES "
                     "(?, ?, ?, ?, ?, ?);",
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
            bind(state.ToString().CStr()), bind(timestamp.UnixNano()), bind(id.CStr()), bind(version.CStr());

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

        *mSession << "SELECT itemID, type, version, indexDigest, state, timestamp FROM imagemanager;", into(rows), now;

        auto itemInfo = std::make_unique<imagemanager::ItemInfo>();

        items.Clear();

        for (const auto& row : rows) {
            ToAos(row, *itemInfo);
            auto err = items.PushBack(*itemInfo);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add item info");
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

        *mSession << "SELECT itemID, type, version, indexDigest, state, timestamp FROM imagemanager WHERE itemID = ?;",
            bind(id.CStr()), into(rows), now;

        auto itemInfo = std::make_unique<imagemanager::ItemInfo>();

        items.Clear();

        for (const auto& row : rows) {
            ToAos(row, *itemInfo);
            auto err = items.PushBack(*itemInfo);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add item info");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::StoreDesiredStatus(const DesiredStatus& desiredStatus)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "UPDATE updatemanager SET desiredStatus = ?;", bind(SerializeDesiredStatus(desiredStatus));

        if (statement.execute() == 0) {
            return ErrorEnum::eNotFound;
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error Database::StoreUpdateState(const updatemanager::UpdateState& state)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "UPDATE updatemanager SET updateState = ?;", bind(state.ToString().CStr());

        if (statement.execute() == 0) {
            return ErrorEnum::eNotFound;
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error Database::GetDesiredStatus(DesiredStatus& desiredStatus)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};
        std::string           desiredStatusStr;

        statement << "SELECT desiredStatus FROM updatemanager;", into(desiredStatusStr), now;

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }

        DeserializeDesiredStatus(desiredStatusStr, desiredStatus);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

RetWithError<updatemanager::UpdateState> Database::GetUpdateState()
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};
        std::string           stateStr;

        statement << "SELECT updateState FROM updatemanager;", into(stateStr), now;

        if (statement.execute() == 0) {
            return {updatemanager::UpdateStateEnum::eNone, AOS_ERROR_WRAP(ErrorEnum::eNotFound)};
        }

        updatemanager::UpdateState state;

        if (auto err = state.FromString(stateStr.c_str()); !err.IsNone()) {
            return {updatemanager::UpdateStateEnum::eNone, AOS_ERROR_WRAP(err)};
        }

        return state;
    } catch (const std::exception& e) {
        return {updatemanager::UpdateStateEnum::eNone, AOS_ERROR_WRAP(common::utils::ToAosError(e))};
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Database::CreateTables()
{
    LOG_DBG() << "Create CM tables if not exist";

    *mSession << "CREATE TABLE IF NOT EXISTS updatemanager ("
                 "desiredStatus TEXT,"
                 "updateState TEXT"
                 ");",
        now;

    *mSession << "INSERT INTO updatemanager (desiredStatus, updateState) "
                 "SELECT ?, ? WHERE NOT EXISTS (SELECT 1 FROM updatemanager);",
        bind("{}"), bind(updatemanager::UpdateState().ToString().CStr()), now;

    *mSession << "CREATE TABLE IF NOT EXISTS launcher_override_envvars ("
                 "itemID TEXT,"
                 "subjectID TEXT,"
                 "instance TEXT,"
                 "variables TEXT"
                 ");",
        now;

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

    *mSession << "CREATE TABLE IF NOT EXISTS imagemanager ("
                 "itemID TEXT,"
                 "type TEXT,"
                 "version TEXT,"
                 "indexDigest TEXT,"
                 "state TEXT,"
                 "timestamp INTEGER,"
                 "PRIMARY KEY(itemID,version)"
                 ");",
        now;

    *mSession << "CREATE TABLE IF NOT EXISTS networks ("
                 "networkID TEXT,"
                 "subnet TEXT,"
                 "vlanID INTEGER,"
                 "PRIMARY KEY(networkID)"
                 ");",
        now;

    *mSession << "CREATE TABLE IF NOT EXISTS hosts ("
                 "networkID TEXT,"
                 "nodeID TEXT,"
                 "ip TEXT,"
                 "PRIMARY KEY(networkID,nodeID),"
                 "FOREIGN KEY(networkID) REFERENCES networks(networkID)"
                 ");",
        now;

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

    *mSession << "CREATE TABLE IF NOT EXISTS pending_connections ("
                 "requesterItemID TEXT,"
                 "requesterSubjectID TEXT,"
                 "requesterInstance INTEGER,"
                 "requesterType TEXT,"
                 "requesterPreinstalled INTEGER,"
                 "nodeID TEXT,"
                 "networkID TEXT,"
                 "requesterIP TEXT,"
                 "requesterSubnet TEXT,"
                 "targetItemID TEXT,"
                 "port TEXT,"
                 "protocol TEXT"
                 ");",
        now;

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
                 "disableRebalancing INTEGER,"
                 "PRIMARY KEY(itemID,subjectID,instance,type,preinstalled,version)"
                 ");",
        now;

    *mSession << "CREATE TABLE IF NOT EXISTS launcher_run_requests ("
                 "itemID TEXT,"
                 "type TEXT,"
                 "version TEXT,"
                 "ownerID TEXT,"
                 "subjectID TEXT,"
                 "subjectType TEXT,"
                 "isUnitSubject INTEGER,"
                 "priority INTEGER,"
                 "numInstances INTEGER,"
                 "labels TEXT"
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
    auto err = dst.mInstanceIdent.mType.FromString(src.get<ToInt(StorageStateInstanceInfoColumns::eType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to parse instance type");

    err = dst.mStateChecksum.Assign(Array<uint8_t>(blob.rawContent(), blob.size()));
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to assign state checksum");
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

    auto err = dst.mInstanceIdent.mType.FromString(src.get<ToInt(NetworkManagerInstanceColumns::eType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to parse instance type");

    dst.mInstanceIdent.mPreinstalled = src.get<ToInt(NetworkManagerInstanceColumns::ePreinstalled)>();
    dst.mNetworkID                   = src.get<ToInt(NetworkManagerInstanceColumns::eNetworkID)>().c_str();
    dst.mNodeID                      = src.get<ToInt(NetworkManagerInstanceColumns::eNodeID)>().c_str();
    dst.mIP                          = src.get<ToInt(NetworkManagerInstanceColumns::eIP)>().c_str();

    DeserializeExposedPorts(src.get<ToInt(NetworkManagerInstanceColumns::eExposedPorts)>(), dst.mExposedPorts);
    DeserializeDNSServers(src.get<ToInt(NetworkManagerInstanceColumns::eDNSServers)>(), dst.mDNSServers);
}

void Database::FromAos(const networkmanager::PendingConnection& src, PendingConnectionRow& dst)
{
    dst.set<ToInt(PendingConnectionColumns::eRequesterItemID)>(src.mRequesterIdent.mItemID.CStr());
    dst.set<ToInt(PendingConnectionColumns::eRequesterSubjectID)>(src.mRequesterIdent.mSubjectID.CStr());
    dst.set<ToInt(PendingConnectionColumns::eRequesterInstance)>(src.mRequesterIdent.mInstance);
    dst.set<ToInt(PendingConnectionColumns::eRequesterType)>(src.mRequesterIdent.mType.ToString().CStr());
    dst.set<ToInt(PendingConnectionColumns::eRequesterPreinstalled)>(src.mRequesterIdent.mPreinstalled);
    dst.set<ToInt(PendingConnectionColumns::eNodeID)>(src.mNodeID.CStr());
    dst.set<ToInt(PendingConnectionColumns::eNetworkID)>(src.mNetworkID.CStr());
    dst.set<ToInt(PendingConnectionColumns::eRequesterIP)>(src.mRequesterIP.CStr());
    dst.set<ToInt(PendingConnectionColumns::eRequesterSubnet)>(src.mRequesterSubnet.CStr());
    dst.set<ToInt(PendingConnectionColumns::eTargetItemID)>(src.mTargetItemID.CStr());
    dst.set<ToInt(PendingConnectionColumns::ePort)>(src.mPort.CStr());
    dst.set<ToInt(PendingConnectionColumns::eProtocol)>(src.mProtocol.CStr());
}

void Database::ToAos(const PendingConnectionRow& src, networkmanager::PendingConnection& dst)
{
    dst.mRequesterIdent.mItemID    = src.get<ToInt(PendingConnectionColumns::eRequesterItemID)>().c_str();
    dst.mRequesterIdent.mSubjectID = src.get<ToInt(PendingConnectionColumns::eRequesterSubjectID)>().c_str();
    dst.mRequesterIdent.mInstance  = src.get<ToInt(PendingConnectionColumns::eRequesterInstance)>();

    auto err = dst.mRequesterIdent.mType.FromString(src.get<ToInt(PendingConnectionColumns::eRequesterType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to parse instance type");

    dst.mRequesterIdent.mPreinstalled = src.get<ToInt(PendingConnectionColumns::eRequesterPreinstalled)>();
    dst.mNodeID                       = src.get<ToInt(PendingConnectionColumns::eNodeID)>().c_str();
    dst.mNetworkID                    = src.get<ToInt(PendingConnectionColumns::eNetworkID)>().c_str();
    dst.mRequesterIP                  = src.get<ToInt(PendingConnectionColumns::eRequesterIP)>().c_str();
    dst.mRequesterSubnet              = src.get<ToInt(PendingConnectionColumns::eRequesterSubnet)>().c_str();
    dst.mTargetItemID                 = src.get<ToInt(PendingConnectionColumns::eTargetItemID)>().c_str();
    dst.mPort                         = src.get<ToInt(PendingConnectionColumns::ePort)>().c_str();
    dst.mProtocol                     = src.get<ToInt(PendingConnectionColumns::eProtocol)>().c_str();
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
    dst.set<ToInt(LauncherInstanceInfoColumns::eDisableRebalancing)>(src.mDisableRebalancing);
}

void Database::ToAos(const LauncherInstanceInfoRow& src, launcher::InstanceInfo& dst)
{
    dst.mInstanceIdent.mItemID    = src.get<ToInt(LauncherInstanceInfoColumns::eItemID)>().c_str();
    dst.mInstanceIdent.mSubjectID = src.get<ToInt(LauncherInstanceInfoColumns::eSubjectID)>().c_str();
    dst.mInstanceIdent.mInstance  = src.get<ToInt(LauncherInstanceInfoColumns::eInstance)>();

    auto err = dst.mInstanceIdent.mType.FromString(src.get<ToInt(LauncherInstanceInfoColumns::eType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to parse instance type");

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

    err = dst.mState.FromString(stateStr.c_str());
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to parse instance state");

    dst.mIsUnitSubject = src.get<ToInt(LauncherInstanceInfoColumns::eIsUnitSubject)>();
    dst.mVersion       = src.get<ToInt(LauncherInstanceInfoColumns::eVersion)>().c_str();
    dst.mOwnerID       = src.get<ToInt(LauncherInstanceInfoColumns::eOwnerID)>().c_str();

    const auto& subjectTypeStr = src.get<ToInt(LauncherInstanceInfoColumns::eSubjectType)>();

    err = dst.mSubjectType.FromString(subjectTypeStr.c_str());
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to parse subject type");

    DeserializeLabels(src.get<ToInt(LauncherInstanceInfoColumns::eLabels)>(), dst.mLabels);
    dst.mPriority           = src.get<ToInt(LauncherInstanceInfoColumns::ePriority)>();
    dst.mDisableRebalancing = src.get<ToInt(LauncherInstanceInfoColumns::eDisableRebalancing)>();
}

void Database::FromAos(const imagemanager::ItemInfo& src, ImageManagerItemInfoRow& dst)
{
    dst.set<ToInt(ImageManagerItemInfoColumns::eItemID)>(src.mItemID.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eVersion)>(src.mVersion.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eType)>(src.mType.ToString().CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eIndexDigest)>(src.mIndexDigest.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eState)>(src.mState.ToString().CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eTimestamp)>(src.mTimestamp.UnixNano());
}

void Database::ToAos(const ImageManagerItemInfoRow& src, imagemanager::ItemInfo& dst)
{
    dst.mItemID = src.get<ToInt(ImageManagerItemInfoColumns::eItemID)>().c_str();
    auto err    = dst.mType.FromString(src.get<ToInt(ImageManagerItemInfoColumns::eType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse item type");
    dst.mVersion     = src.get<ToInt(ImageManagerItemInfoColumns::eVersion)>().c_str();
    dst.mIndexDigest = src.get<ToInt(ImageManagerItemInfoColumns::eIndexDigest)>().c_str();
    err              = dst.mState.FromString(src.get<ToInt(ImageManagerItemInfoColumns::eState)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse item state");
    auto timestamp = src.get<ToInt(ImageManagerItemInfoColumns::eTimestamp)>();
    dst.mTimestamp = Time::Unix(timestamp / Time::cSeconds.Nanoseconds(), timestamp % Time::cSeconds.Nanoseconds());
}

void Database::FromAos(const EnvVarsInstanceInfo& src, LauncherOverrideEnvVarsRow& dst)
{
    std::string itemID;
    std::string subjectID;
    std::string instanceStr;

    if (src.mItemID.HasValue()) {
        itemID = src.mItemID.GetValue().CStr();
    }

    if (src.mSubjectID.HasValue()) {
        subjectID = src.mSubjectID.GetValue().CStr();
    }

    if (src.mInstance.HasValue()) {
        instanceStr = std::to_string(src.mInstance.GetValue());
    }

    dst.set<ToInt(LauncherOverrideEnvVarsColumns::eItemID)>(itemID);
    dst.set<ToInt(LauncherOverrideEnvVarsColumns::eSubjectID)>(subjectID);
    dst.set<ToInt(LauncherOverrideEnvVarsColumns::eInstance)>(instanceStr);
    dst.set<ToInt(LauncherOverrideEnvVarsColumns::eVariables)>(SerializeEnvVars(src.mVariables));
}

void Database::ToAos(const LauncherOverrideEnvVarsRow& src, EnvVarsInstanceInfo& dst)
{
    dst.mItemID.Reset();
    dst.mSubjectID.Reset();
    dst.mInstance.Reset();
    dst.mVariables.Clear();

    const auto& itemID = src.get<ToInt(LauncherOverrideEnvVarsColumns::eItemID)>();
    if (!itemID.empty()) {
        dst.mItemID.EmplaceValue();

        auto err = dst.mItemID.GetValue().Assign(itemID.c_str());
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to assign item ID");
    }

    const auto& subjectID = src.get<ToInt(LauncherOverrideEnvVarsColumns::eSubjectID)>();
    if (!subjectID.empty()) {
        dst.mSubjectID.EmplaceValue();

        auto err = dst.mSubjectID.GetValue().Assign(subjectID.c_str());
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to assign subject ID");
    }

    const auto& instanceStr = src.get<ToInt(LauncherOverrideEnvVarsColumns::eInstance)>();
    if (!instanceStr.empty()) {
        uint64_t instanceValue = std::stoull(instanceStr);
        dst.mInstance.SetValue(instanceValue);
    }

    const auto& variablesJson = src.get<ToInt(LauncherOverrideEnvVarsColumns::eVariables)>();
    DeserializeEnvVars(variablesJson, dst.mVariables);
}

void Database::FromAos(const launcher::RunInstanceRequest& src, LauncherRunRequestRow& dst)
{
    dst.set<ToInt(LauncherRunRequestColumns::eItemID)>(src.mItemID.CStr());
    dst.set<ToInt(LauncherRunRequestColumns::eType)>(src.mUpdateItemType.ToString().CStr());
    dst.set<ToInt(LauncherRunRequestColumns::eVersion)>(src.mVersion.CStr());
    dst.set<ToInt(LauncherRunRequestColumns::eOwnerID)>(src.mOwnerID.CStr());
    dst.set<ToInt(LauncherRunRequestColumns::eSubjectID)>(src.mSubjectInfo.mSubjectID.CStr());
    dst.set<ToInt(LauncherRunRequestColumns::eSubjectType)>(src.mSubjectInfo.mSubjectType.ToString().CStr());
    dst.set<ToInt(LauncherRunRequestColumns::eIsUnitSubject)>(src.mSubjectInfo.mIsUnitSubject);
    dst.set<ToInt(LauncherRunRequestColumns::ePriority)>(src.mPriority);
    dst.set<ToInt(LauncherRunRequestColumns::eNumInstances)>(src.mNumInstances);
    dst.set<ToInt(LauncherRunRequestColumns::eLabels)>(SerializeLabels(src.mLabels));
}

void Database::ToAos(const LauncherRunRequestRow& src, launcher::RunInstanceRequest& dst)
{
    dst.mItemID       = src.get<ToInt(LauncherRunRequestColumns::eItemID)>().c_str();
    dst.mVersion      = src.get<ToInt(LauncherRunRequestColumns::eVersion)>().c_str();
    dst.mOwnerID      = src.get<ToInt(LauncherRunRequestColumns::eOwnerID)>().c_str();
    dst.mPriority     = src.get<ToInt(LauncherRunRequestColumns::ePriority)>();
    dst.mNumInstances = src.get<ToInt(LauncherRunRequestColumns::eNumInstances)>();

    auto err = dst.mUpdateItemType.FromString(src.get<ToInt(LauncherRunRequestColumns::eType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to parse update item type");

    dst.mSubjectInfo.mSubjectID = src.get<ToInt(LauncherRunRequestColumns::eSubjectID)>().c_str();
    err = dst.mSubjectInfo.mSubjectType.FromString(src.get<ToInt(LauncherRunRequestColumns::eSubjectType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "failed to parse subject type");
    dst.mSubjectInfo.mIsUnitSubject = src.get<ToInt(LauncherRunRequestColumns::eIsUnitSubject)>();

    DeserializeLabels(src.get<ToInt(LauncherRunRequestColumns::eLabels)>(), dst.mLabels);
}

} // namespace aos::cm::database
