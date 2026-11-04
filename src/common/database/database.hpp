/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_DATABASE_DATABASE_HPP_
#define AOS_COMMON_DATABASE_DATABASE_HPP_

#include <memory>
#include <mutex>
#include <string>

#include <Poco/Data/Session.h>
#include <Poco/Tuple.h>

#include <core/common/imagemanager/itf/storage.hpp>

namespace aos::common::database {

/**
 * Common database class. Implements storage interfaces shared between applications. Tables used by this class should
 * be created by the application database migration.
 */
class Database : public virtual common::imagemanager::StorageItf {
public:
    //
    // common::imagemanager::StorageItf interface
    //

    /**
     * Adds deployable item.
     *
     * @param item deployable item info.
     * @return Error: eAlreadyExist if item with the same ID and version exists.
     */
    Error AddDeployableItem(const common::imagemanager::DeployableItemInfo& item) override;

    /**
     * Updates deployable item. Blob references of the item are kept.
     *
     * @param item deployable item info.
     * @return Error: eNotFound if item doesn't exist.
     */
    Error UpdateDeployableItem(const common::imagemanager::DeployableItemInfo& item) override;

    /**
     * Removes deployable item and all its blob references.
     *
     * @param itemID item ID.
     * @param version item version.
     * @return Error: eNotFound if item doesn't exist.
     */
    Error RemoveDeployableItem(const String& itemID, const String& version) override;

    /**
     * Returns deployable item.
     *
     * @param itemID item ID.
     * @param version item version.
     * @param[out] item deployable item info.
     * @return Error: eNotFound if item doesn't exist.
     */
    Error GetDeployableItem(
        const String& itemID, const String& version, common::imagemanager::DeployableItemInfo& item) override;

    /**
     * Returns all deployable items.
     *
     * @param[out] items deployable items.
     * @return Error.
     */
    Error GetAllDeployableItems(Array<common::imagemanager::DeployableItemInfo>& items) override;

    /**
     * Returns number of stored deployable items in all states.
     *
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetDeployableItemsCount() override;

    /**
     * Adds blob reference to deployable item. Does nothing if the reference already exists.
     *
     * @param itemID item ID.
     * @param version item version.
     * @param blob blob info.
     * @return Error: eNotFound if item doesn't exist.
     */
    Error AddItemBlob(
        const String& itemID, const String& version, const common::imagemanager::ItemBlobInfo& blob) override;

    /**
     * Returns blobs referenced by deployable item.
     *
     * @param itemID item ID.
     * @param version item version.
     * @param[out] blobs blobs info.
     * @return Error: eNotFound if item doesn't exist.
     */
    Error GetItemBlobs(
        const String& itemID, const String& version, Array<common::imagemanager::ItemBlobInfo>& blobs) override;

    /**
     * Checks if blob is referenced by any existing deployable item, regardless of the item state. Blob references
     * left without an item are ignored.
     *
     * @param digest blob digest.
     * @return RetWithError<bool>.
     */
    RetWithError<bool> IsBlobUsed(const String& digest) override;

protected:
    /**
     * Creates database instance.
     */
    Database() = default;

    /**
     * Destructor.
     */
    virtual ~Database() = default;

    std::unique_ptr<Poco::Data::Session> mSession;
    mutable std::mutex                   mMutex;

private:
    enum class ImageManagerItemColumns : int {
        eItemID = 0,
        eType,
        eVersion,
        ePreinstalled,
        eState,
        eErrorAosCode,
        eErrorSysCode,
        eErrorMessage,
        eRootDigest,
        eTimestamp,
    };
    using ImageManagerItemRow = Poco::Tuple<std::string, std::string, std::string, bool, std::string, int32_t, int32_t,
        std::string, std::string, uint64_t>;

    enum class ImageManagerBlobColumns : int {
        eItemID = 0,
        eVersion,
        eDigest,
        eMediaType,
        eSize,
        eIsLayer,
        eDiffID,
    };
    using ImageManagerBlobRow
        = Poco::Tuple<std::string, std::string, std::string, std::string, uint64_t, bool, std::string>;

    bool DeployableItemExists(const String& itemID, const String& version) const;

    static void FromAos(const common::imagemanager::DeployableItemInfo& src, ImageManagerItemRow& dst);
    static void ToAos(const ImageManagerItemRow& src, common::imagemanager::DeployableItemInfo& dst);

    static void FromAos(const String& itemID, const String& version, const common::imagemanager::ItemBlobInfo& src,
        ImageManagerBlobRow& dst);
    static void ToAos(const ImageManagerBlobRow& src, common::imagemanager::ItemBlobInfo& dst);
};

} // namespace aos::common::database

#endif
