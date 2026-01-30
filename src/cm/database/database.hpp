/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_DATABASE_DATABASE_HPP_
#define AOS_CM_DATABASE_DATABASE_HPP_

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <Poco/Data/LOB.h>
#include <Poco/Data/Session.h>
#include <Poco/JSON/Object.h>

#include <cm/networkmanager/itf/storage.hpp>
#include <common/migration/migration.hpp>
#include <core/cm/database/itf/database.hpp>

#include "config.hpp"

namespace aos::cm::database {

/**
 * Database class.
 */
class Database : public DatabaseItf, public networkmanager::StorageItf {
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
     * Initializes database.
     *
     * @param config database configuration.
     * @return Error.
     */
    Error Init(const Config& config);

    //
    // storagestate::StorageItf interface
    //

    /**
     * Adds storage state instance info.
     *
     * @param info storage state instance info to add.
     * @return Error.
     */
    Error AddStorageStateInfo(const storagestate::InstanceInfo& info) override;

    /**
     * Removes storage state instance info.
     *
     * @param instanceIdent instance ident to remove.
     * @return Error.
     */
    Error RemoveStorageStateInfo(const InstanceIdent& instanceIdent) override;

    /**
     * Returns all storage state instance infos.
     *
     * @param infos[out] array to return storage state instance infos.
     * @return Error.
     */
    Error GetAllStorageStateInfo(Array<storagestate::InstanceInfo>& infos) override;

    /**
     * Returns storage state instance info by instance ident.
     *
     * @param instanceIdent instance ident to get storage state info for.
     * @param info[out] storage state instance info result.
     * @return Error.
     */
    Error GetStorageStateInfo(const InstanceIdent& instanceIdent, storagestate::InstanceInfo& info) override;

    /**
     * Updates storage state instance info.
     *
     * @param info storage state instance info to update.
     * @return Error.
     */
    Error UpdateStorageStateInfo(const storagestate::InstanceInfo& info) override;

    //
    // networkmanager::StorageItf interface
    //

    /**
     * Adds network.
     *
     * @param network Network.
     * @return Error.
     */
    Error AddNetwork(const networkmanager::Network& network) override;

    /**
     * Adds host.
     *
     * @param networkID Network ID.
     * @param host Host.
     * @return Error.
     */
    Error AddHost(const String& networkID, const networkmanager::Host& host) override;

    /**
     * Adds instance.
     *
     * @param instance Instance.
     * @return Error.
     */
    Error AddInstance(const networkmanager::Instance& instance) override;

    /**
     * Gets networks.
     *
     * @param[out] networks Networks.
     * @return Error.
     */
    Error GetNetworks(Array<networkmanager::Network>& networks) override;

    /**
     * Gets hosts.
     *
     * @param networkID Network ID.
     * @param[out] hosts Hosts.
     * @return Error.
     */
    Error GetHosts(const String& networkID, Array<networkmanager::Host>& hosts) override;

    /**
     * Gets instances.
     *
     * @param networkID Network ID.
     * @param nodeID Node ID.
     * @param[out] instances Instances.
     * @return Error.
     */
    Error GetInstances(
        const String& networkID, const String& nodeID, Array<networkmanager::Instance>& instances) override;

    /**
     * Removes network.
     *
     * @param networkID Network ID.
     * @return Error.
     */
    Error RemoveNetwork(const String& networkID) override;

    /**
     * Removes host.
     *
     * @param networkID Network ID.
     * @param nodeID Node ID.
     * @return Error.
     */
    Error RemoveHost(const String& networkID, const String& nodeID) override;

    /**
     * Removes network instance.
     *
     * @param instanceIdent Instance identifier.
     * @return Error.
     */
    Error RemoveNetworkInstance(const InstanceIdent& instanceIdent) override;

    //
    // launcher::StorageItf interface
    //

    /**
     * Adds launcher instance.
     *
     * @param info Instance information.
     * @return Error.
     */
    Error AddInstance(const launcher::InstanceInfo& info) override;

    /**
     * Updates launcher instance.
     *
     * @param info Instance information.
     * @return Error.
     */
    Error UpdateInstance(const launcher::InstanceInfo& info) override;

    /**
     * Gets launcher instance.
     *
     * @param instanceID Instance identifier.
     * @param[out] info Instance information.
     * @return Error.
     */
    Error GetInstance(const InstanceIdent& instanceID, launcher::InstanceInfo& info) const override;

    /**
     * Gets all active launcher instances.
     *
     * @param[out] instances Instances.
     * @return Error.
     */
    Error GetActiveInstances(Array<launcher::InstanceInfo>& instances) const override;

    /**
     * Removes launcher instance.
     *
     * @param instanceIdent Instance identifier.
     * @return Error.
     */
    Error RemoveInstance(const InstanceIdent& instanceIdent) override;

    //
    // imagemanager::StorageItf interface
    //

    /**
     * Adds item.
     *
     * @param item Item info.
     * @return Error.
     */
    Error AddItem(const imagemanager::ItemInfo& item) override;

    /**
     * Removes item.
     *
     * @param id ID.
     * @param version Version.
     * @return Error.
     */
    Error RemoveItem(const String& id, const String& version) override;

    /**
     * Updates item state.
     *
     * @param id ID.
     * @param version Version.
     * @param state Item state.
     * @param timestamp Timestamp.
     * @return Error.
     */
    Error UpdateItemState(const String& id, const String& version, ItemState state, Time timestamp = {}) override;

    /**
     * Gets all items info.
     *
     * @param items Items info.
     * @return Error.
     */
    Error GetAllItemsInfos(Array<imagemanager::ItemInfo>& items) override;

    /**
     * Gets items info by ID.
     *
     * @param id Item ID.
     * @param items Items info.
     * @return Error.
     */
    Error GetItemInfos(const String& id, Array<imagemanager::ItemInfo>& items) override;

private:
    static constexpr int  cVersion    = 0;
    static constexpr auto cDBFileName = "cm.db";

    enum class StorageStateInstanceInfoColumns : int {
        eItemID = 0,
        eSubjectID,
        eInstance,
        eType,
        ePreinstalled,
        eStorageQuota,
        eStateQuota,
        eStateChecksum
    };
    using StorageStateInstanceInfoRow
        = Poco::Tuple<std::string, std::string, uint64_t, std::string, bool, size_t, size_t, Poco::Data::BLOB>;

    enum class NetworkManagerNetworkColumns : int { eNetworkID = 0, eSubnet, eVlanID };
    using NetworkManagerNetworkRow = Poco::Tuple<std::string, std::string, uint64_t>;

    enum class NetworkManagerHostColumns : int { eNetworkID = 0, eNodeID, eIP };
    using NetworkManagerHostRow = Poco::Tuple<std::string, std::string, std::string>;

    enum class NetworkManagerInstanceColumns : int {
        eItemID = 0,
        eSubjectID,
        eInstance,
        eType,
        ePreinstalled,
        eNetworkID,
        eNodeID,
        eIP,
        eExposedPorts,
        eDNSServers
    };
    using NetworkManagerInstanceRow = Poco::Tuple<std::string, std::string, uint64_t, std::string, bool, std::string,
        std::string, std::string, std::string, std::string>;

    enum class LauncherInstanceInfoColumns : int {
        eItemID = 0,
        eSubjectID,
        eInstance,
        eType,
        ePreinstalled,
        eManifestDigest,
        eNodeID,
        ePrevNodeID,
        eRuntimeID,
        eUID,
        eGID,
        eTimestamp,
        eState,
        eIsUnitSubject,
        eVersion,
        eOwnerID,
        eSubjectType,
        eLabels,
        ePriority
    };
    using LauncherInstanceInfoRow = Poco::Tuple<std::string, std::string, uint64_t, std::string, bool, std::string,
        std::string, std::string, std::string, uint32_t, uint32_t, uint64_t, std::string, bool, std::string,
        std::string, std::string, std::string, size_t>;

    enum class ImageManagerItemInfoColumns : int { eItemID = 0, eVersion, eIndexDigest, eState, eTimestamp };
    using ImageManagerItemInfoRow = Poco::Tuple<std::string, std::string, std::string, int, uint64_t>;

    // make virtual for unit tests
    virtual int GetVersion() const;
    void        CreateTables();

    static void FromAos(const storagestate::InstanceInfo& src, StorageStateInstanceInfoRow& dst);
    static void ToAos(const StorageStateInstanceInfoRow& src, storagestate::InstanceInfo& dst);

    static void FromAos(const networkmanager::Network& src, NetworkManagerNetworkRow& dst);
    static void ToAos(const NetworkManagerNetworkRow& src, networkmanager::Network& dst);

    static void FromAos(const String& networkID, const networkmanager::Host& src, NetworkManagerHostRow& dst);
    static void ToAos(const NetworkManagerHostRow& src, networkmanager::Host& dst);

    static void FromAos(const networkmanager::Instance& src, NetworkManagerInstanceRow& dst);
    static void ToAos(const NetworkManagerInstanceRow& src, networkmanager::Instance& dst);

    static void FromAos(const launcher::InstanceInfo& src, LauncherInstanceInfoRow& dst);
    static void ToAos(const LauncherInstanceInfoRow& src, launcher::InstanceInfo& dst);

    static void FromAos(const imagemanager::ItemInfo& src, ImageManagerItemInfoRow& dst);
    static void ToAos(const ImageManagerItemInfoRow& src, imagemanager::ItemInfo& dst);

    std::unique_ptr<Poco::Data::Session>        mSession;
    std::optional<common::migration::Migration> mDatabase;
    mutable std::mutex                          mMutex;
};

} // namespace aos::cm::database

#endif
