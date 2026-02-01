/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <Poco/Data/SQLite/Connector.h>
#include <Poco/Data/SQLite/SQLiteException.h>
#include <Poco/Data/Session.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Path.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <sm/logger/logmodule.hpp>

#include "database.hpp"

using namespace Poco::Data::Keywords;

namespace aos::sm::database {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

template <typename E>
constexpr int ToInt(E e)
{
    return static_cast<int>(e);
}

Time ConvertTimestamp(uint64_t timestamp)
{
    const auto seconds = timestamp / Time::cSeconds.Nanoseconds();
    const auto nanos   = static_cast<int64_t>(timestamp % Time::cSeconds.Nanoseconds());

    return Time::Unix(seconds, nanos);
}

std::string SerializeEnvVars(const EnvVarArray& envVars)
{
    Poco::JSON::Array jsonArray;

    for (const auto& envVar : envVars) {
        Poco::JSON::Object obj;
        obj.set("name", envVar.mName.CStr());
        obj.set("value", envVar.mValue.CStr());
        jsonArray.add(obj);
    }

    return common::utils::Stringify(jsonArray);
}

void DeserializeEnvVars(const std::string& jsonStr, EnvVarArray& envVars)
{
    if (jsonStr.empty()) {
        envVars.Clear();
        return;
    }

    Poco::JSON::Parser parser;

    auto jsonArray = parser.parse(jsonStr).extract<Poco::JSON::Array::Ptr>();
    if (jsonArray == nullptr) {
        return;
    }

    envVars.Clear();

    for (const auto& item : *jsonArray) {
        const auto obj = item.extract<Poco::JSON::Object::Ptr>();
        if (obj == nullptr) {
            continue;
        }

        EnvVar envVar;
        AOS_ERROR_CHECK_AND_THROW(envVar.mName.Assign(obj->getValue<std::string>("name").c_str()));
        AOS_ERROR_CHECK_AND_THROW(envVar.mValue.Assign(obj->getValue<std::string>("value").c_str()));
        AOS_ERROR_CHECK_AND_THROW(envVars.PushBack(envVar), "can't add env var");
    }
}

std::string SerializeNetworkParameters(const Optional<InstanceNetworkParameters>& params)
{
    if (!params.HasValue()) {
        return "";
    }

    const auto& p = params.GetValue();

    Poco::JSON::Object obj;
    obj.set("networkID", p.mNetworkID.CStr());
    obj.set("subnet", p.mSubnet.CStr());
    obj.set("ip", p.mIP.CStr());

    Poco::JSON::Array dnsArray;
    for (const auto& dns : p.mDNSServers) {
        dnsArray.add(dns.CStr());
    }
    obj.set("dnsServers", dnsArray);

    Poco::JSON::Array rulesArray;
    for (const auto& rule : p.mFirewallRules) {
        Poco::JSON::Object ruleObj;
        ruleObj.set("dstIP", rule.mDstIP.CStr());
        ruleObj.set("dstPort", rule.mDstPort.CStr());
        ruleObj.set("proto", rule.mProto.CStr());
        ruleObj.set("srcIP", rule.mSrcIP.CStr());
        rulesArray.add(ruleObj);
    }
    obj.set("firewallRules", rulesArray);

    return common::utils::Stringify(obj);
}

void DeserializeNetworkParameters(const std::string& jsonStr, Optional<InstanceNetworkParameters>& params)
{
    if (jsonStr.empty()) {
        params.Reset();
        return;
    }

    Poco::JSON::Parser parser;
    auto               obj = parser.parse(jsonStr).extract<Poco::JSON::Object::Ptr>();
    if (obj == nullptr) {
        params.Reset();
        return;
    }

    params.EmplaceValue();
    auto& p = params.GetValue();

    AOS_ERROR_CHECK_AND_THROW(p.mNetworkID.Assign(obj->getValue<std::string>("networkID").c_str()));
    AOS_ERROR_CHECK_AND_THROW(p.mSubnet.Assign(obj->getValue<std::string>("subnet").c_str()));
    AOS_ERROR_CHECK_AND_THROW(p.mIP.Assign(obj->getValue<std::string>("ip").c_str()));

    if (obj->has("dnsServers")) {
        auto dnsArray = obj->getArray("dnsServers");
        for (const auto& dns : *dnsArray) {
            AOS_ERROR_CHECK_AND_THROW(p.mDNSServers.EmplaceBack(dns.convert<std::string>().c_str()), "can't add DNS");
        }
    }

    if (obj->has("firewallRules")) {
        auto rulesArray = obj->getArray("firewallRules");
        for (const auto& item : *rulesArray) {
            const auto ruleObj = item.extract<Poco::JSON::Object::Ptr>();
            if (ruleObj == nullptr) {
                continue;
            }

            FirewallRule rule;
            AOS_ERROR_CHECK_AND_THROW(rule.mDstIP.Assign(ruleObj->getValue<std::string>("dstIP").c_str()));
            AOS_ERROR_CHECK_AND_THROW(rule.mDstPort.Assign(ruleObj->getValue<std::string>("dstPort").c_str()));
            AOS_ERROR_CHECK_AND_THROW(rule.mProto.Assign(ruleObj->getValue<std::string>("proto").c_str()));
            AOS_ERROR_CHECK_AND_THROW(rule.mSrcIP.Assign(ruleObj->getValue<std::string>("srcIP").c_str()));
            AOS_ERROR_CHECK_AND_THROW(p.mFirewallRules.PushBack(rule), "can't add firewall rule");
        }
    }
}

std::string SerializeMonitoringParams(const Optional<InstanceMonitoringParams>& params)
{
    if (!params.HasValue()) {
        return "";
    }

    const auto& p = params.GetValue();

    Poco::JSON::Object obj;

    if (p.mAlertRules.HasValue()) {
        const auto& rules = p.mAlertRules.GetValue();

        Poco::JSON::Object alertObj;

        if (rules.mRAM.HasValue()) {
            Poco::JSON::Object ramObj;
            ramObj.set("minThreshold", rules.mRAM.GetValue().mMinThreshold);
            ramObj.set("maxThreshold", rules.mRAM.GetValue().mMaxThreshold);
            ramObj.set("minTimeout", rules.mRAM.GetValue().mMinTimeout.Nanoseconds());
            alertObj.set("ram", ramObj);
        }

        if (rules.mCPU.HasValue()) {
            Poco::JSON::Object cpuObj;
            cpuObj.set("minThreshold", rules.mCPU.GetValue().mMinThreshold);
            cpuObj.set("maxThreshold", rules.mCPU.GetValue().mMaxThreshold);
            cpuObj.set("minTimeout", rules.mCPU.GetValue().mMinTimeout.Nanoseconds());
            alertObj.set("cpu", cpuObj);
        }

        if (rules.mDownload.HasValue()) {
            Poco::JSON::Object downloadObj;
            downloadObj.set("minThreshold", rules.mDownload.GetValue().mMinThreshold);
            downloadObj.set("maxThreshold", rules.mDownload.GetValue().mMaxThreshold);
            downloadObj.set("minTimeout", rules.mDownload.GetValue().mMinTimeout.Nanoseconds());
            alertObj.set("download", downloadObj);
        }

        if (rules.mUpload.HasValue()) {
            Poco::JSON::Object uploadObj;
            uploadObj.set("minThreshold", rules.mUpload.GetValue().mMinThreshold);
            uploadObj.set("maxThreshold", rules.mUpload.GetValue().mMaxThreshold);
            uploadObj.set("minTimeout", rules.mUpload.GetValue().mMinTimeout.Nanoseconds());
            alertObj.set("upload", uploadObj);
        }

        if (!rules.mPartitions.IsEmpty()) {
            Poco::JSON::Array partitionsArray;
            for (const auto& partition : rules.mPartitions) {
                Poco::JSON::Object partObj;
                partObj.set("name", partition.mName.CStr());
                partObj.set("minThreshold", partition.mMinThreshold);
                partObj.set("maxThreshold", partition.mMaxThreshold);
                partObj.set("minTimeout", partition.mMinTimeout.Nanoseconds());
                partitionsArray.add(partObj);
            }
            alertObj.set("partitions", partitionsArray);
        }

        obj.set("alertRules", alertObj);
    }

    return common::utils::Stringify(obj);
}

void DeserializeMonitoringParams(const std::string& jsonStr, Optional<InstanceMonitoringParams>& params)
{
    if (jsonStr.empty()) {
        params.Reset();
        return;
    }

    Poco::JSON::Parser parser;
    auto               obj = parser.parse(jsonStr).extract<Poco::JSON::Object::Ptr>();
    if (obj == nullptr) {
        params.Reset();
        return;
    }

    params.EmplaceValue();
    auto& p = params.GetValue();

    if (obj->has("alertRules")) {
        auto alertObj = obj->getObject("alertRules");
        p.mAlertRules.EmplaceValue();
        auto& rules = p.mAlertRules.GetValue();

        if (alertObj->has("ram")) {
            auto ramObj = alertObj->getObject("ram");
            rules.mRAM.EmplaceValue();
            rules.mRAM.GetValue().mMinThreshold = ramObj->getValue<uint8_t>("minThreshold");
            rules.mRAM.GetValue().mMaxThreshold = ramObj->getValue<uint8_t>("maxThreshold");
            rules.mRAM.GetValue().mMinTimeout   = Duration(ramObj->getValue<int64_t>("minTimeout"));
        }

        if (alertObj->has("cpu")) {
            auto cpuObj = alertObj->getObject("cpu");
            rules.mCPU.EmplaceValue();
            rules.mCPU.GetValue().mMinThreshold = cpuObj->getValue<uint8_t>("minThreshold");
            rules.mCPU.GetValue().mMaxThreshold = cpuObj->getValue<uint8_t>("maxThreshold");
            rules.mCPU.GetValue().mMinTimeout   = Duration(cpuObj->getValue<int64_t>("minTimeout"));
        }

        if (alertObj->has("download")) {
            auto downloadObj = alertObj->getObject("download");
            rules.mDownload.EmplaceValue();
            rules.mDownload.GetValue().mMinThreshold = downloadObj->getValue<uint64_t>("minThreshold");
            rules.mDownload.GetValue().mMaxThreshold = downloadObj->getValue<uint64_t>("maxThreshold");
            rules.mDownload.GetValue().mMinTimeout   = Duration(downloadObj->getValue<int64_t>("minTimeout"));
        }

        if (alertObj->has("upload")) {
            auto uploadObj = alertObj->getObject("upload");
            rules.mUpload.EmplaceValue();
            rules.mUpload.GetValue().mMinThreshold = uploadObj->getValue<uint64_t>("minThreshold");
            rules.mUpload.GetValue().mMaxThreshold = uploadObj->getValue<uint64_t>("maxThreshold");
            rules.mUpload.GetValue().mMinTimeout   = Duration(uploadObj->getValue<int64_t>("minTimeout"));
        }

        if (alertObj->has("partitions")) {
            auto partitionsArray = alertObj->getArray("partitions");
            for (const auto& item : *partitionsArray) {
                const auto partObj = item.extract<Poco::JSON::Object::Ptr>();
                if (partObj == nullptr) {
                    continue;
                }

                PartitionAlertRule rule;
                AOS_ERROR_CHECK_AND_THROW(rule.mName.Assign(partObj->getValue<std::string>("name").c_str()));
                rule.mMinThreshold = partObj->getValue<uint8_t>("minThreshold");
                rule.mMaxThreshold = partObj->getValue<uint8_t>("maxThreshold");
                rule.mMinTimeout   = Duration(partObj->getValue<int64_t>("minTimeout"));
                AOS_ERROR_CHECK_AND_THROW(rules.mPartitions.PushBack(rule), "can't add partition rule");
            }
        }
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

Error Database::Init(const std::string& workDir, const common::config::Migration& migrationConfig)
{
    LOG_DBG() << "Init database" << Log::Field("workDir", workDir.c_str());

    if (mSession && mSession->isConnected()) {
        return ErrorEnum::eNone;
    }

    try {
        auto dirPath = std::filesystem::path(workDir);
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }

        const auto dbPath = Poco::Path(workDir, cDBFileName);
        mSession          = std::make_unique<Poco::Data::Session>("SQLite", dbPath.toString());

        CreateTables();

        mMigration.emplace(*mSession, migrationConfig.mMigrationPath, migrationConfig.mMergedMigrationPath);
        mMigration->MigrateToVersion(sVersion);

        return ErrorEnum::eNone;

    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

/***********************************************************************************************************************
 * sm::imagemanager::StorageItf implementation
 **********************************************************************************************************************/

Error Database::AddUpdateItem(const imagemanager::UpdateItemData& updateItem)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Add update item" << Log::Field("id", updateItem.mID) << Log::Field("type", updateItem.mType)
              << Log::Field("version", updateItem.mVersion) << Log::Field("state", updateItem.mState);

    try {
        ItemDataRow row;

        FromAos(updateItem, row);

        *mSession
            << "INSERT INTO items (itemID, type, version, manifestDigest, state, timestamp) VALUES (?, ?, ?, ?, ?, ?);",
            bind(row), now;

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error Database::UpdateUpdateItem(const imagemanager::UpdateItemData& updateItem)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Update update item" << Log::Field("id", updateItem.mID) << Log::Field("type", updateItem.mType)
              << Log::Field("version", updateItem.mVersion) << Log::Field("state", updateItem.mState);

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "UPDATE items SET manifestDigest = ?, state = ?, timestamp = ? WHERE itemID = ? AND type = ? AND "
                     "version = ?;",
            bind(updateItem.mManifestDigest.CStr()), bind(updateItem.mState.ToString().CStr()),
            bind(updateItem.mTimestamp.UnixNano()), bind(updateItem.mID.CStr()),
            bind(updateItem.mType.ToString().CStr()), bind(updateItem.mVersion.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error Database::RemoveUpdateItem(const String& itemID, const String& version)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Remove update item" << Log::Field("id", itemID) << Log::Field("version", version);

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM items WHERE itemID = ? AND version = ?;", bind(itemID.CStr()), bind(version.CStr());

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error Database::GetUpdateItem(const String& itemID, Array<imagemanager::UpdateItemData>& itemData)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get update item" << Log::Field("id", itemID);

    try {
        std::vector<ItemDataRow> rows;
        Poco::Data::Statement    statement {*mSession};

        statement << "SELECT * FROM items WHERE itemID = ?;", bind(itemID.CStr()), into(rows);

        if (statement.execute() == 0) {
            return ErrorEnum::eNotFound;
        }

        for (const auto& row : rows) {
            imagemanager::UpdateItemData updateItem;

            ToAos(row, updateItem);

            if (auto err = itemData.PushBack(updateItem); !err.IsNone()) {
                return AOS_ERROR_WRAP(Error(err, "db items count exceeds application limit"));
            }
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error Database::GetAllUpdateItems(Array<imagemanager::UpdateItemData>& itemsData)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get all update items";

    try {
        std::vector<ItemDataRow> rows;
        Poco::Data::Statement    statement {*mSession};

        statement << "SELECT * FROM items;", into(rows);

        statement.execute();

        for (const auto& row : rows) {
            imagemanager::UpdateItemData updateItem;

            ToAos(row, updateItem);

            if (auto err = itemsData.PushBack(updateItem); !err.IsNone()) {
                return AOS_ERROR_WRAP(Error(err, "db items count exceeds application limit"));
            }
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

RetWithError<size_t> Database::GetUpdateItemsCount()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get update items count";

    try {
        Poco::Data::Statement statement {*mSession};
        size_t                count;

        statement << "SELECT COUNT(*) FROM items;", into(count), now;

        return {count, ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {0, AOS_ERROR_WRAP(common::utils::ToAosError(e))};
    }
}

/***********************************************************************************************************************
 * sm::launcher::StorageItf implementation
 **********************************************************************************************************************/

Error Database::GetAllInstancesInfos([[maybe_unused]] Array<InstanceInfo>& infos)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get all instances infos";

    try {
        std::vector<InstanceInfoRow> rows;

        *mSession << "SELECT itemID, subjectID, instance, type, preinstalled, version, manifestDigest, "
                     "runtimeID, ownerID, subjectType,uid, gid, priority, storagePath, statePath, "
                     "envVars,networkParameters, monitoringParams "
                     "FROM instances;",
            into(rows), now;

        auto instanceInfo = std::make_unique<InstanceInfo>();

        for (const auto& row : rows) {
            ToAos(row, *instanceInfo);

            if (auto err = infos.PushBack(*instanceInfo); !err.IsNone()) {
                return AOS_ERROR_WRAP(Error(err, "db instances count exceeds application limit"));
            }
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::UpdateInstanceInfo(const InstanceInfo& info)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Update instance info" << Log::Field("instance", static_cast<const InstanceIdent&>(info));

    try {
        InstanceInfoRow row;

        FromAos(info, row);

        *mSession
            << "INSERT OR REPLACE INTO instances (itemID, subjectID, instance, type, preinstalled, version, "
               "manifestDigest, runtimeID, ownerID, subjectType, uid, gid, priority, storagePath, statePath, envVars, "
               "networkParameters, monitoringParams) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveInstanceInfo(const InstanceIdent& ident)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Remove instance info" << Log::Field("instance", static_cast<const InstanceIdent&>(ident));

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM instances WHERE itemID = ? AND subjectID = ? AND instance = ? AND type = ? "
                     "AND preinstalled = ?;",
            bind(ident.mItemID.CStr()), bind(ident.mSubjectID.CStr()), bind(ident.mInstance),
            bind(ident.mType.ToString().CStr()), bind(ident.mPreinstalled);

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * sm::networkmanager::StorageItf implementation
 **********************************************************************************************************************/

Error Database::RemoveNetworkInfo(const String& networkID)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Remove network" << Log::Field("networkID", networkID);

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM network WHERE networkID = ?;", bind(networkID.CStr());

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::AddNetworkInfo(const sm::networkmanager::NetworkInfo& info)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Add network info" << Log::Field("networkID", info.mNetworkID);

    try {
        NetworkInfoRow row;

        FromAos(info, row);

        *mSession << "INSERT INTO network (networkID, ip, subnet, vlanID, vlanIfName, bridgeIfName) VALUES (?, ?, ?, "
                     "?, ?, ?);",
            bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetNetworksInfo(Array<sm::networkmanager::NetworkInfo>& networks) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get all networks";

    try {
        std::vector<NetworkInfoRow> rows;

        *mSession << "SELECT networkID, ip, subnet, vlanID, vlanIfName, bridgeIfName FROM network;", into(rows), now;

        auto networkInfo = std::make_unique<sm::networkmanager::NetworkInfo>();

        for (const auto& row : rows) {
            ToAos(row, *networkInfo);

            if (auto err = networks.PushBack(*networkInfo); !err.IsNone()) {
                return AOS_ERROR_WRAP(Error(err, "db network count exceeds application limit"));
            }
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::SetTrafficMonitorData(const String& chain, const Time& time, uint64_t value)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Set traffic monitor data" << Log::Field("chain", chain) << Log::Field("time", time)
              << Log::Field("value", value);

    try {
        *mSession << "INSERT OR REPLACE INTO trafficmonitor values(?, ?, ?);", bind(chain.CStr()),
            bind(time.UnixNano()), bind(value), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetTrafficMonitorData(const String& chain, Time& time, uint64_t& value) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get traffic monitor data" << Log::Field("chain", chain);

    try {
        Poco::Data::Statement statement {*mSession};

        uint64_t dbTime = 0;

        statement << "SELECT time, value FROM trafficmonitor WHERE chain = ?;", bind(chain.CStr()), into(dbTime),
            into(value);

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }

        time = ConvertTimestamp(dbTime);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveTrafficMonitorData(const String& chain)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Remove traffic monitor data" << Log::Field("chain", chain);

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM trafficmonitor WHERE chain = ?;", bind(chain.CStr());

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::AddInstanceNetworkInfo(const sm::networkmanager::InstanceNetworkInfo& info)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Add instance network info" << Log::Field("instanceID", info.mInstanceID)
              << Log::Field("networkID", info.mNetworkID);

    try {
        *mSession << "INSERT INTO instancenetwork (instanceID, networkID) VALUES (?, ?);",
            bind(info.mInstanceID.CStr()), bind(info.mNetworkID.CStr()), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveInstanceNetworkInfo(const String& instanceID)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Remove instance network info" << Log::Field("instanceID", instanceID);

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "DELETE FROM instancenetwork WHERE instanceID = ?;", bind(instanceID.CStr());

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetInstanceNetworksInfo(Array<sm::networkmanager::InstanceNetworkInfo>& networks) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get all instance networks";

    try {
        std::vector<std::pair<std::string, std::string>> result;

        *mSession << "SELECT instanceID, networkID FROM instancenetwork", into(result), now;

        for (const auto& [instanceID, networkID] : result) {
            if (auto err = networks.EmplaceBack(instanceID.c_str(), networkID.c_str()); !err.IsNone()) {
                LOG_WRN() << "Failed to add instance network info" << Log::Field("instanceID", instanceID.c_str())
                          << Log::Field("networkID", networkID.c_str()) << Log::Field(err);
                return AOS_ERROR_WRAP(Error(err, "db instance networks count exceeds application limit"));
            }
        }
    } catch (const std::exception& e) {
        LOG_WRN() << "Failed to get instance networks info" << Log::Field(common::utils::ToAosError(e));
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * sm::alerts::StorageItf implementation
 **********************************************************************************************************************/

Error Database::SetJournalCursor(const String& cursor)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Set journal cursor" << Log::Field("cursor", cursor);

    try {
        *mSession << "UPDATE config SET cursor = ?;", bind(cursor.CStr()), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetJournalCursor(String& cursor) const
{
    std::lock_guard lock {mMutex};

    try {
        std::string dbCursor;

        *mSession << "SELECT cursor FROM config;", into(dbCursor), now;

        cursor = dbCursor.c_str();

        LOG_DBG() << "Get journal cursor" << Log::Field("cursor", cursor);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

bool Database::TableExist(const std::string& tableName)
{
    size_t count {0};

    Poco::Data::Statement statement {*mSession};

    statement << "SELECT count(*) FROM sqlite_master WHERE name = ? and type='table'", bind(tableName), into(count);

    if (statement.execute() == 0) {
        return false;
    }

    return count > 0;
}

void Database::CreateTables()
{
    LOG_DBG() << "Create tables";

    if (!TableExist("config")) {
        *mSession << "CREATE TABLE config (cursor TEXT);", now;
        *mSession << "INSERT INTO config (cursor) VALUES ('');", now;
    }

    *mSession << "CREATE TABLE IF NOT EXISTS network ("
                 "networkID TEXT NOT NULL PRIMARY KEY, "
                 "ip TEXT, "
                 "subnet TEXT, "
                 "vlanID INTEGER, "
                 "vlanIfName TEXT);",
        now;

    *mSession << "CREATE TABLE IF NOT EXISTS services ("
                 "id TEXT NOT NULL , "
                 "version TEXT, "
                 "providerID TEXT, "
                 "imagePath TEXT, "
                 "manifestDigest BLOB, "
                 "state INTEGER, "
                 "timestamp TIMESTAMP, "
                 "size INTEGER, "
                 "GID INTEGER, "
                 "PRIMARY KEY(id, version));",
        now;

    *mSession << "CREATE TABLE IF NOT EXISTS trafficmonitor ("
                 "chain TEXT NOT NULL PRIMARY KEY, "
                 "time TIMESTAMP, "
                 "value INTEGER)",
        now;

    *mSession << "CREATE TABLE IF NOT EXISTS layers ("
                 "digest TEXT NOT NULL PRIMARY KEY, "
                 "unpackedDigest TEXT, "
                 "layerId TEXT, "
                 "path TEXT, "
                 "osVersion TEXT, "
                 "version TEXT, "
                 "timestamp TIMESTAMP, "
                 "state INTEGER, "
                 "size INTEGER)",
        now;

    *mSession << "CREATE TABLE IF NOT EXISTS instances ("
                 "instanceID TEXT NOT NULL PRIMARY KEY, "
                 "serviceID TEXT, "
                 "subjectID TEXT, "
                 "instance INTEGER, "
                 "uid INTEGER, "
                 "priority INTEGER, "
                 "storagePath TEXT, "
                 "statePath TEXT, "
                 "network BLOB)",
        now;
}

void Database::FromAos(const imagemanager::UpdateItemData& src, ItemDataRow& dst)
{
    dst.set<ToInt(ItemDataColumns::eItemID)>(src.mID.CStr());
    dst.set<ToInt(ItemDataColumns::eType)>(src.mType.ToString().CStr());
    dst.set<ToInt(ItemDataColumns::eVersion)>(src.mVersion.CStr());
    dst.set<ToInt(ItemDataColumns::eManifestDigest)>(src.mManifestDigest.CStr());
    dst.set<ToInt(ItemDataColumns::eState)>(src.mState.ToString().CStr());
    dst.set<ToInt(ItemDataColumns::eTimestamp)>(src.mTimestamp.UnixNano());
}

void Database::ToAos(const ItemDataRow& src, imagemanager::UpdateItemData& dst)
{
    auto err = dst.mID.Assign(src.get<ToInt(ItemDataColumns::eItemID)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to assign item ID");

    err = dst.mType.FromString(src.get<ToInt(ItemDataColumns::eType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse item type");

    err = dst.mVersion.Assign(src.get<ToInt(ItemDataColumns::eVersion)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to assign item version");

    err = dst.mManifestDigest.Assign(src.get<ToInt(ItemDataColumns::eManifestDigest)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to assign manifest digest");

    err = dst.mState.FromString(src.get<ToInt(ItemDataColumns::eState)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse item state");

    dst.mTimestamp = ConvertTimestamp(src.get<ToInt(ItemDataColumns::eTimestamp)>());
}

void Database::FromAos(const InstanceInfo& src, InstanceInfoRow& dst)
{
    dst.set<ToInt(InstanceInfoColumns::eItemID)>(src.mItemID.CStr());
    dst.set<ToInt(InstanceInfoColumns::eSubjectID)>(src.mSubjectID.CStr());
    dst.set<ToInt(InstanceInfoColumns::eInstance)>(src.mInstance);
    dst.set<ToInt(InstanceInfoColumns::eType)>(src.mType.ToString().CStr());
    dst.set<ToInt(InstanceInfoColumns::ePreinstalled)>(src.mPreinstalled);
    dst.set<ToInt(InstanceInfoColumns::eVersion)>(src.mVersion.CStr());
    dst.set<ToInt(InstanceInfoColumns::eManifestDigest)>(src.mManifestDigest.CStr());
    dst.set<ToInt(InstanceInfoColumns::eRuntimeID)>(src.mRuntimeID.CStr());
    dst.set<ToInt(InstanceInfoColumns::eOwnerID)>(src.mOwnerID.CStr());
    dst.set<ToInt(InstanceInfoColumns::eSubjectType)>(src.mSubjectType.ToString().CStr());
    dst.set<ToInt(InstanceInfoColumns::eUID)>(src.mUID);
    dst.set<ToInt(InstanceInfoColumns::eGID)>(src.mGID);
    dst.set<ToInt(InstanceInfoColumns::ePriority)>(src.mPriority);
    dst.set<ToInt(InstanceInfoColumns::eStoragePath)>(src.mStoragePath.CStr());
    dst.set<ToInt(InstanceInfoColumns::eStatePath)>(src.mStatePath.CStr());
    dst.set<ToInt(InstanceInfoColumns::eEnvVars)>(SerializeEnvVars(src.mEnvVars));
    dst.set<ToInt(InstanceInfoColumns::eNetworkParameters)>(SerializeNetworkParameters(src.mNetworkParameters));
    dst.set<ToInt(InstanceInfoColumns::eMonitoringParams)>(SerializeMonitoringParams(src.mMonitoringParams));
}

void Database::ToAos(const InstanceInfoRow& src, InstanceInfo& dst)
{
    dst.mItemID         = src.get<ToInt(InstanceInfoColumns::eItemID)>().c_str();
    dst.mSubjectID      = src.get<ToInt(InstanceInfoColumns::eSubjectID)>().c_str();
    dst.mInstance       = src.get<ToInt(InstanceInfoColumns::eInstance)>();
    dst.mPreinstalled   = src.get<ToInt(InstanceInfoColumns::ePreinstalled)>();
    dst.mVersion        = src.get<ToInt(InstanceInfoColumns::eVersion)>().c_str();
    dst.mManifestDigest = src.get<ToInt(InstanceInfoColumns::eManifestDigest)>().c_str();
    dst.mRuntimeID      = src.get<ToInt(InstanceInfoColumns::eRuntimeID)>().c_str();
    dst.mOwnerID        = src.get<ToInt(InstanceInfoColumns::eOwnerID)>().c_str();
    dst.mUID            = src.get<ToInt(InstanceInfoColumns::eUID)>();
    dst.mGID            = src.get<ToInt(InstanceInfoColumns::eGID)>();
    dst.mPriority       = src.get<ToInt(InstanceInfoColumns::ePriority)>();
    dst.mStoragePath    = src.get<ToInt(InstanceInfoColumns::eStoragePath)>().c_str();
    dst.mStatePath      = src.get<ToInt(InstanceInfoColumns::eStatePath)>().c_str();

    AOS_ERROR_CHECK_AND_THROW(
        dst.mType.FromString(src.get<ToInt(InstanceInfoColumns::eType)>().c_str()), "failed to parse instance type");

    AOS_ERROR_CHECK_AND_THROW(dst.mSubjectType.FromString(src.get<ToInt(InstanceInfoColumns::eSubjectType)>().c_str()),
        "failed to parse subject type");

    DeserializeEnvVars(src.get<ToInt(InstanceInfoColumns::eEnvVars)>(), dst.mEnvVars);
    DeserializeNetworkParameters(src.get<ToInt(InstanceInfoColumns::eNetworkParameters)>(), dst.mNetworkParameters);
    DeserializeMonitoringParams(src.get<ToInt(InstanceInfoColumns::eMonitoringParams)>(), dst.mMonitoringParams);
}

void Database::FromAos(const sm::networkmanager::NetworkInfo& src, NetworkInfoRow& dst)
{
    dst.set<ToInt(NetworkInfoColumns::eNetworkID)>(src.mNetworkID.CStr());
    dst.set<ToInt(NetworkInfoColumns::eIP)>(src.mIP.CStr());
    dst.set<ToInt(NetworkInfoColumns::eSubnet)>(src.mSubnet.CStr());
    dst.set<ToInt(NetworkInfoColumns::eVlanID)>(src.mVlanID);
    dst.set<ToInt(NetworkInfoColumns::eVlanIfName)>(src.mVlanIfName.CStr());
    dst.set<ToInt(NetworkInfoColumns::eBridgeIfName)>(src.mBridgeIfName.CStr());
}

void Database::ToAos(const NetworkInfoRow& src, sm::networkmanager::NetworkInfo& dst)
{
    dst.mNetworkID    = src.get<ToInt(NetworkInfoColumns::eNetworkID)>().c_str();
    dst.mIP           = src.get<ToInt(NetworkInfoColumns::eIP)>().c_str();
    dst.mSubnet       = src.get<ToInt(NetworkInfoColumns::eSubnet)>().c_str();
    dst.mVlanID       = src.get<ToInt(NetworkInfoColumns::eVlanID)>();
    dst.mVlanIfName   = src.get<ToInt(NetworkInfoColumns::eVlanIfName)>().c_str();
    dst.mBridgeIfName = src.get<ToInt(NetworkInfoColumns::eBridgeIfName)>().c_str();
}

} // namespace aos::sm::database
