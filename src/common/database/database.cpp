/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <vector>

#include <Poco/Data/SQLite/SQLiteException.h>
#include <Poco/Data/Statement.h>
#include <Poco/Data/Transaction.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>

#include "database.hpp"

using namespace Poco::Data::Keywords;

namespace aos::common::database {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

template <typename E>
constexpr int ToInt(E e)
{
    return static_cast<int>(e);
}

} // namespace

/***********************************************************************************************************************
 * common::imagemanager::StorageItf implementation
 **********************************************************************************************************************/

Error Database::AddDeployableItem(const common::imagemanager::DeployableItemInfo& item)
{
    LOG_DBG() << "Add deployable item" << Log::Field("itemID", item.mItemID) << Log::Field("version", item.mVersion)
              << Log::Field("state", item.mState);

    std::lock_guard lock {mMutex};

    try {
        ImageManagerItemRow row;

        FromAos(item, row);
        *mSession << "INSERT INTO imagemanager_items (itemID, type, version, preinstalled, state, errorAosCode, "
                     "errorSysCode, errorMessage, rootDigest, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            bind(row), now;
    } catch (const Poco::Data::SQLite::ConstraintViolationException&) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eAlreadyExist, "deployable item already exists"));
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::UpdateDeployableItem(const common::imagemanager::DeployableItemInfo& item)
{
    LOG_DBG() << "Update deployable item" << Log::Field("itemID", item.mItemID) << Log::Field("version", item.mVersion)
              << Log::Field("state", item.mState);

    std::lock_guard lock {mMutex};

    try {
        ImageManagerItemRow   row;
        Poco::Data::Statement statement {*mSession};

        FromAos(item, row);

        statement << "UPDATE imagemanager_items SET type = ?, preinstalled = ?, state = ?, errorAosCode = ?, "
                     "errorSysCode = ?, errorMessage = ?, rootDigest = ?, timestamp = ? "
                     "WHERE itemID = ? AND version = ?;",
            bind(row.get<ToInt(ImageManagerItemColumns::eType)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::ePreinstalled)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::eState)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::eErrorAosCode)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::eErrorSysCode)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::eErrorMessage)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::eRootDigest)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::eTimestamp)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::eItemID)>()),
            bind(row.get<ToInt(ImageManagerItemColumns::eVersion)>());

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::RemoveDeployableItem(const String& itemID, const String& version)
{
    LOG_DBG() << "Remove deployable item" << Log::Field("itemID", itemID) << Log::Field("version", version);

    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Transaction transaction(*mSession);
        Poco::Data::Statement   statement {*mSession};

        statement << "DELETE FROM imagemanager_items WHERE itemID = ? AND version = ?;", bind(itemID.CStr()),
            bind(version.CStr());

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }

        *mSession << "DELETE FROM imagemanager_blobs WHERE itemID = ? AND version = ?;", bind(itemID.CStr()),
            bind(version.CStr()), now;

        transaction.commit();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetDeployableItem(
    const String& itemID, const String& version, common::imagemanager::DeployableItemInfo& item)
{
    LOG_DBG() << "Get deployable item" << Log::Field("itemID", itemID) << Log::Field("version", version);

    std::lock_guard lock {mMutex};

    try {
        ImageManagerItemRow   row;
        Poco::Data::Statement statement {*mSession};

        statement << "SELECT itemID, type, version, preinstalled, state, errorAosCode, errorSysCode, errorMessage, "
                     "rootDigest, timestamp FROM imagemanager_items WHERE itemID = ? AND version = ?;",
            bind(itemID.CStr()), bind(version.CStr()), into(row);

        if (statement.execute() == 0) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }

        ToAos(row, item);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetAllDeployableItems(Array<common::imagemanager::DeployableItemInfo>& items)
{
    LOG_DBG() << "Get all deployable items";

    std::lock_guard lock {mMutex};

    try {
        std::vector<ImageManagerItemRow> rows;

        *mSession << "SELECT itemID, type, version, preinstalled, state, errorAosCode, errorSysCode, errorMessage, "
                     "rootDigest, timestamp FROM imagemanager_items ORDER BY rowid;",
            into(rows), now;

        auto item = std::make_unique<common::imagemanager::DeployableItemInfo>();

        items.Clear();

        for (const auto& row : rows) {
            ToAos(row, *item);
            auto err = items.PushBack(*item);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add deployable item");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

RetWithError<size_t> Database::GetDeployableItemsCount()
{
    LOG_DBG() << "Get deployable items count";

    std::lock_guard lock {mMutex};

    try {
        size_t count = 0;

        *mSession << "SELECT COUNT(*) FROM imagemanager_items;", into(count), now;

        return {count, ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {0, AOS_ERROR_WRAP(common::utils::ToAosError(e))};
    }
}

Error Database::AddItemBlob(const String& itemID, const String& version, const common::imagemanager::ItemBlobInfo& blob)
{
    LOG_DBG() << "Add item blob" << Log::Field("itemID", itemID) << Log::Field("version", version)
              << Log::Field("digest", blob.mDigest);

    std::lock_guard lock {mMutex};

    try {
        if (!DeployableItemExists(itemID, version)) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }

        ImageManagerBlobRow row;

        FromAos(itemID, version, blob, row);
        *mSession << "INSERT OR IGNORE INTO imagemanager_blobs (itemID, version, digest, mediaType, size, isLayer, "
                     "diffID) VALUES (?, ?, ?, ?, ?, ?, ?);",
            bind(row), now;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetItemBlobs(
    const String& itemID, const String& version, Array<common::imagemanager::ItemBlobInfo>& blobs)
{
    LOG_DBG() << "Get item blobs" << Log::Field("itemID", itemID) << Log::Field("version", version);

    std::lock_guard lock {mMutex};

    try {
        if (!DeployableItemExists(itemID, version)) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }

        std::vector<ImageManagerBlobRow> rows;

        *mSession << "SELECT itemID, version, digest, mediaType, size, isLayer, diffID FROM imagemanager_blobs "
                     "WHERE itemID = ? AND version = ? ORDER BY rowid;",
            bind(itemID.CStr()), bind(version.CStr()), into(rows), now;

        auto blob = std::make_unique<common::imagemanager::ItemBlobInfo>();

        blobs.Clear();

        for (const auto& row : rows) {
            ToAos(row, *blob);
            auto err = blobs.PushBack(*blob);
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(err), "can't add item blob");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

RetWithError<bool> Database::IsBlobUsed(const String& digest)
{
    LOG_DBG() << "Check if blob is used" << Log::Field("digest", digest);

    std::lock_guard lock {mMutex};

    try {
        int used = 0;

        *mSession << "SELECT EXISTS (SELECT 1 FROM imagemanager_blobs b JOIN imagemanager_items i "
                     "ON b.itemID = i.itemID AND b.version = i.version WHERE b.digest = ?);",
            bind(digest.CStr()), into(used), now;

        return {used != 0, ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {false, AOS_ERROR_WRAP(common::utils::ToAosError(e))};
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

bool Database::DeployableItemExists(const String& itemID, const String& version) const
{
    size_t count = 0;

    *mSession << "SELECT COUNT(*) FROM imagemanager_items WHERE itemID = ? AND version = ?;", bind(itemID.CStr()),
        bind(version.CStr()), into(count), now;

    return count != 0;
}

void Database::FromAos(const common::imagemanager::DeployableItemInfo& src, ImageManagerItemRow& dst)
{
    dst.set<ToInt(ImageManagerItemColumns::eItemID)>(src.mItemID.CStr());
    dst.set<ToInt(ImageManagerItemColumns::eType)>(src.mType.ToString().CStr());
    dst.set<ToInt(ImageManagerItemColumns::eVersion)>(src.mVersion.CStr());
    dst.set<ToInt(ImageManagerItemColumns::ePreinstalled)>(src.mPreinstalled);
    dst.set<ToInt(ImageManagerItemColumns::eState)>(src.mState.ToString().CStr());
    dst.set<ToInt(ImageManagerItemColumns::eErrorAosCode)>(static_cast<int32_t>(src.mError.Value()));
    dst.set<ToInt(ImageManagerItemColumns::eErrorSysCode)>(src.mError.Code());
    dst.set<ToInt(ImageManagerItemColumns::eErrorMessage)>(src.mError.Message());
    dst.set<ToInt(ImageManagerItemColumns::eRootDigest)>(src.mRootDigest.CStr());
    dst.set<ToInt(ImageManagerItemColumns::eTimestamp)>(src.mTimestamp.UnixNano());
}

void Database::ToAos(const ImageManagerItemRow& src, common::imagemanager::DeployableItemInfo& dst)
{
    dst.mItemID = src.get<ToInt(ImageManagerItemColumns::eItemID)>().c_str();

    auto err = dst.mType.FromString(src.get<ToInt(ImageManagerItemColumns::eType)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse item type");

    dst.mVersion      = src.get<ToInt(ImageManagerItemColumns::eVersion)>().c_str();
    dst.mPreinstalled = src.get<ToInt(ImageManagerItemColumns::ePreinstalled)>();

    err = dst.mState.FromString(src.get<ToInt(ImageManagerItemColumns::eState)>().c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse item state");

    const auto& message = src.get<ToInt(ImageManagerItemColumns::eErrorMessage)>();

    dst.mError = Error(static_cast<ErrorEnum>(src.get<ToInt(ImageManagerItemColumns::eErrorAosCode)>()),
        src.get<ToInt(ImageManagerItemColumns::eErrorSysCode)>(), message.empty() ? nullptr : message.c_str());

    dst.mRootDigest = src.get<ToInt(ImageManagerItemColumns::eRootDigest)>().c_str();

    auto timestamp = src.get<ToInt(ImageManagerItemColumns::eTimestamp)>();
    dst.mTimestamp = Time::Unix(timestamp / Time::cSeconds.Nanoseconds(), timestamp % Time::cSeconds.Nanoseconds());
}

void Database::FromAos(const String& itemID, const String& version, const common::imagemanager::ItemBlobInfo& src,
    ImageManagerBlobRow& dst)
{
    dst.set<ToInt(ImageManagerBlobColumns::eItemID)>(itemID.CStr());
    dst.set<ToInt(ImageManagerBlobColumns::eVersion)>(version.CStr());
    dst.set<ToInt(ImageManagerBlobColumns::eDigest)>(src.mDigest.CStr());
    dst.set<ToInt(ImageManagerBlobColumns::eMediaType)>(src.mMediaType.CStr());
    dst.set<ToInt(ImageManagerBlobColumns::eSize)>(src.mSize);
    dst.set<ToInt(ImageManagerBlobColumns::eIsLayer)>(src.mIsLayer);
    dst.set<ToInt(ImageManagerBlobColumns::eDiffID)>(src.mDiffID.CStr());
}

void Database::ToAos(const ImageManagerBlobRow& src, common::imagemanager::ItemBlobInfo& dst)
{
    dst.mDigest    = src.get<ToInt(ImageManagerBlobColumns::eDigest)>().c_str();
    dst.mMediaType = src.get<ToInt(ImageManagerBlobColumns::eMediaType)>().c_str();
    dst.mSize      = src.get<ToInt(ImageManagerBlobColumns::eSize)>();
    dst.mIsLayer   = src.get<ToInt(ImageManagerBlobColumns::eIsLayer)>();
    dst.mDiffID    = src.get<ToInt(ImageManagerBlobColumns::eDiffID)>().c_str();
}

} // namespace aos::common::database
