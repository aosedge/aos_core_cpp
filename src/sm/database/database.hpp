/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_DATABASE_DATABASE_HPP_
#define AOS_SM_DATABASE_DATABASE_HPP_

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <Poco/Data/Session.h>
#include <Poco/Tuple.h>

#include <core/sm/launcher/itf/storage.hpp>
#include <core/sm/networkmanager/itf/storage.hpp>

#include <common/migration/migration.hpp>
#include <sm/alerts/itf/instanceinfoprovider.hpp>
#include <sm/alerts/itf/storage.hpp>
#include <sm/config/config.hpp>
#include <sm/logprovider/itf/instanceidprovider.hpp>

namespace aos::sm::database {

class Database : public sm::launcher::StorageItf,
                 public sm::networkmanager::StorageItf,
                 public sm::alerts::StorageItf,
                 public sm::alerts::InstanceInfoProviderItf,
                 public sm::logprovider::InstanceIDProviderItf {
public:
    /**
     * Creates database instance.
     */
    Database();

    /**
     * Destructor.
     */
    ~Database();

    /**
     * Initializes certificate info storage.
     *
     * @param workDir working directory.
     * @param migrationConfig migration config.
     * @return Error.
     */
    Error Init(const std::string& workDir, const common::config::Migration& migrationConfig);

    // sm::launcher::StorageItf interface

    /**
     * Gets all instances infos from storage.
     *
     * @param[out] infos array to store instance infos.
     * @return Error.
     */
    Error GetAllInstancesInfos(Array<InstanceInfo>& infos) override;

    /**
     * Updates instance info in storage. Inserts a new record
     * if it does not exist.
     *
     * @param info instance info to store.
     * @return Error.
     */
    Error UpdateInstanceInfo(const InstanceInfo& info) override;

    /**
     * Deletes instance info from storage.
     *
     * @param ident instance ident.
     * @return Error.
     */
    Error RemoveInstanceInfo(const InstanceIdent& ident) override;

    // sm::networkmanager::StorageItf interface

    /**
     * Removes network info from storage.
     *
     * @param networkID network ID to remove.
     * @return Error.
     */
    Error RemoveNetworkInfo(const String& networkID) override;

    /**
     * Adds network info to storage.
     *
     * @param info network info.
     * @return Error.
     */
    Error AddNetworkInfo(const sm::networkmanager::NetworkInfo& info) override;

    /**
     * Returns network information.
     *
     * @param networks[out] network information result.
     * @return Error.
     */
    Error GetNetworksInfo(Array<sm::networkmanager::NetworkInfo>& networks) const override;

    /**
     * Adds instance network info to storage.
     *
     * @param info instance network information.
     * @return Error.
     */
    Error AddInstanceNetworkInfo(const sm::networkmanager::InstanceNetworkInfo& info) override;

    /**
     * Removes instance network info from storage.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error RemoveInstanceNetworkInfo(const String& instanceID) override;

    /**
     * Returns instance network info.
     *
     * @param[out] networks instance network information.
     * @return Error.
     */
    Error GetInstanceNetworksInfo(Array<sm::networkmanager::InstanceNetworkInfo>& networks) const override;

    /**
     * Sets traffic monitor data.
     *
     * @param chain chain.
     * @param time time.
     * @param value value.
     * @return Error.
     */
    Error SetTrafficMonitorData(const String& chain, const Time& time, uint64_t value) override;

    /**
     * Returns traffic monitor data.
     *
     * @param chain chain.
     * @param time[out] time.
     * @param value[out] value.
     * @return Error.
     */
    Error GetTrafficMonitorData(const String& chain, Time& time, uint64_t& value) const override;

    /**
     * Removes traffic monitor data.
     *
     * @param chain chain.
     * @return Error.
     */
    Error RemoveTrafficMonitorData(const String& chain) override;

    // sm::alerts::StorageItf interface

    /**
     * Sets journal cursor.
     *
     * @param cursor journal cursor.
     * @return Error.
     */
    Error SetJournalCursor(const String& cursor) override;

    /**
     * Gets journal cursor.
     *
     * @param cursor[out] journal cursor.
     * @return Error.
     */
    Error GetJournalCursor(String& cursor) const override;

    // sm::alerts::InstanceInfoProviderItf interface

    /**
     * Gets instance info.
     *
     * @param id instance id.
     * @param[out] instanceData service instance data.
     * @return Error.
     */
    Error GetInstanceInfoByID(const String& id, alerts::ServiceInstanceData& instanceData) override;

    // sm::logprovider::InstanceIDProviderItf interface

    /**
     * Gets instance ids.
     *
     * @param filter log filter.
     * @param[out] instanceIDs instance IDs.
     * @return Error.
     */
    Error GetInstanceIDs(const LogFilter& filter, std::vector<std::string>& instanceIDs) override;

private:
    static constexpr int  sVersion    = 3;
    static constexpr auto cDBFileName = "servicemanager.db";

    // Instance info columns
    enum class InstanceInfoColumns : int {
        eItemID = 0,
        eSubjectID,
        eInstance,
        eType,
        eVersion,
        eManifestDigest,
        eRuntimeID,
        eOwnerID,
        eSubjectType,
        eUID,
        eGID,
        ePriority,
        eStoragePath,
        eStatePath,
        eEnvVars,
        eNetworkParameters,
        eMonitoringParams
    };
    using InstanceInfoRow = Poco::Tuple<std::string, std::string, uint64_t, std::string, std::string, std::string,
        std::string, std::string, std::string, uint32_t, uint32_t, uint64_t, std::string, std::string, std::string,
        std::string, std::string>;

    // Network info columns
    enum class NetworkInfoColumns : int { eNetworkID = 0, eIP, eSubnet, eVlanID, eVlanIfName, eBridgeIfName };
    using NetworkInfoRow = Poco::Tuple<std::string, std::string, std::string, uint64_t, std::string, std::string>;

    bool TableExist(const std::string& tableName);
    void CreateConfigTable();
    void CreateTables();

    // FromAos/ToAos conversion methods
    static void FromAos(const InstanceInfo& src, InstanceInfoRow& dst);
    static void ToAos(const InstanceInfoRow& src, InstanceInfo& dst);

    static void FromAos(const sm::networkmanager::NetworkInfo& src, NetworkInfoRow& dst);
    static void ToAos(const NetworkInfoRow& src, sm::networkmanager::NetworkInfo& dst);

    mutable std::unique_ptr<Poco::Data::Session> mSession;
    std::optional<common::migration::Migration>  mMigration;
    mutable std::mutex                           mMutex;
};

} // namespace aos::sm::database

#endif
