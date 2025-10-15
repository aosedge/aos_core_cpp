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

#include <common/logger/logmodule.hpp>
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

std::string SerializeDNSServers(const Array<StaticString<cHostNameLen>>& dnsServers)
{
    Poco::JSON::Array dnsJSON;

    for (const auto& server : dnsServers) {
        dnsJSON.add(server.CStr());
    }

    return common::utils::Stringify(dnsJSON);
}

void DeserializeDNSServers(const std::string& jsonStr, Array<StaticString<cHostNameLen>>& dnsServers)
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

void SerializePlatformInfo(const PlatformInfo& src, Poco::JSON::Object& dst)
{
    dst.set("architecture", src.mArchInfo.mArchitecture.CStr());

    if (src.mArchInfo.mVariant.HasValue()) {
        dst.set("variant", src.mArchInfo.mVariant.GetValue().CStr());
    }

    dst.set("os", src.mOSInfo.mOS.CStr());

    if (src.mOSInfo.mVersion.HasValue()) {
        dst.set("osVersion", src.mOSInfo.mVersion.GetValue().CStr());
    }

    Poco::JSON::Array featuresArray;

    for (const auto& feature : src.mOSInfo.mFeatures) {
        featuresArray.add(feature.CStr());
    }

    dst.set("features", featuresArray);
}

void DeserializePlatformInfo(const Poco::JSON::Object& src, PlatformInfo& dst)
{
    AOS_ERROR_CHECK_AND_THROW(dst.mArchInfo.mArchitecture.Assign(src.getValue<std::string>("architecture").c_str()));

    if (src.has("variant")) {
        dst.mArchInfo.mVariant.SetValue(src.getValue<std::string>("variant").c_str());
    }

    AOS_ERROR_CHECK_AND_THROW(dst.mOSInfo.mOS.Assign(src.getValue<std::string>("os").c_str()));

    if (src.has("osVersion")) {
        dst.mOSInfo.mVersion.SetValue(src.getValue<std::string>("osVersion").c_str());
    }

    if (src.has("features")) {
        auto featuresArray = src.getArray("features");

        for (const auto& featureJson : *featuresArray) {
            AOS_ERROR_CHECK_AND_THROW(
                dst.mOSInfo.mFeatures.PushBack(featureJson.convert<std::string>().c_str()), "can't add feature");
        }
    }
}

void SerializeAosImageInfo(const aos::ImageInfo& src, Poco::JSON::Object& dst)
{
    dst.set("imageID", src.mImageID.CStr());
    SerializePlatformInfo(src, dst);
}

void DeserializeAosImageInfo(const Poco::JSON::Object& src, aos::ImageInfo& dst)
{
    AOS_ERROR_CHECK_AND_THROW(dst.mImageID.Assign(src.getValue<std::string>("imageID").c_str()));
    DeserializePlatformInfo(src, dst);
}

std::string SerializeImages(const Array<imagemanager::storage::ImageInfo>& images)
{
    Poco::JSON::Array imagesJSON;
    auto              sha256Hex = std::make_unique<StaticString<crypto::cSHA256Size * 2 + 1>>();

    for (const auto& image : images) {
        Poco::JSON::Object imageObj;

        imageObj.set("url", image.mURL.CStr());
        imageObj.set("path", image.mPath.CStr());
        imageObj.set("size", static_cast<uint64_t>(image.mSize));

        sha256Hex->Clear();
        AOS_ERROR_CHECK_AND_THROW(sha256Hex->ByteArrayToHex(image.mSHA256), "failed to convert SHA256 to hex");
        imageObj.set("sha256", sha256Hex->CStr());

        Poco::JSON::Array metadataArray;

        for (const auto& metadata : image.mMetadata) {
            metadataArray.add(metadata.CStr());
        }

        imageObj.set("metadata", metadataArray);

        SerializeAosImageInfo(image, imageObj);

        imagesJSON.add(imageObj);
    }

    return common::utils::Stringify(imagesJSON);
}

void DeserializeImages(const std::string& json, Array<imagemanager::storage::ImageInfo>& images)
{
    Poco::JSON::Parser parser;

    auto imagesJSON = parser.parse(json).extract<Poco::JSON::Array::Ptr>();
    if (imagesJSON == nullptr) {
        AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse images array");
    }

    auto imageInfo = std::make_unique<imagemanager::storage::ImageInfo>();
    auto metadata  = std::make_unique<StaticString<cJSONMaxLen>>();

    images.Clear();

    for (const auto& imageJson : *imagesJSON) {
        const auto imageObj = imageJson.extract<Poco::JSON::Object::Ptr>();
        if (imageObj == nullptr) {
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse image object");
        }

        imageInfo->mURL  = imageObj->getValue<std::string>("url").c_str();
        imageInfo->mPath = imageObj->getValue<std::string>("path").c_str();
        imageInfo->mSize = imageObj->getValue<uint64_t>("size");

        auto sha256Hex = imageObj->getValue<std::string>("sha256");
        AOS_ERROR_CHECK_AND_THROW(
            String(sha256Hex.c_str()).HexToByteArray(imageInfo->mSHA256), "failed to convert hex to SHA256");

        auto metadataArray = imageObj->getArray("metadata");
        if (metadataArray == nullptr) {
            AOS_ERROR_CHECK_AND_THROW(AOS_ERROR_WRAP(ErrorEnum::eFailed), "failed to parse metadata array");
        }

        for (const auto& metadataJson : *metadataArray) {
            AOS_ERROR_CHECK_AND_THROW(
                metadata->Assign(metadataJson.convert<std::string>().c_str()), "can't assign metadata");
            AOS_ERROR_CHECK_AND_THROW(imageInfo->mMetadata.PushBack(*metadata), "can't add metadata");
        }

        DeserializeAosImageInfo(*imageObj, *imageInfo);

        AOS_ERROR_CHECK_AND_THROW(images.PushBack(*imageInfo), "can't add image info");
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
        *mSession << "INSERT INTO storagestate (itemID, subjectID, instance, storageQuota, "
                     "stateQuota, stateChecksum) VALUES (?, ?, ?, ?, ?, ?);",
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

        statement << "DELETE FROM storagestate WHERE itemID = ? AND subjectID = ? AND instance = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance);

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
        *mSession << "SELECT itemID, subjectID, instance, storageQuota, stateQuota, stateChecksum FROM "
                     "storagestate;",
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

        statement << "SELECT itemID, subjectID, instance, storageQuota, stateQuota, stateChecksum FROM "
                     "storagestate WHERE itemID = ? AND subjectID = ? AND instance = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance),
            into(row);

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
                     "itemID = ? AND subjectID = ? AND instance = ?;",
            bind(info.mStorageQuota), bind(info.mStateQuota), bind(checksumBlob),
            bind(info.mInstanceIdent.mItemID.CStr()), bind(info.mInstanceIdent.mSubjectID.CStr()),
            bind(info.mInstanceIdent.mInstance);

        if (statement.execute() == 0) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * imagemanager::storage::StorageItf implementation
 **********************************************************************************************************************/

Error Database::SetItemState(const String& id, const String& version, imagemanager::storage::ItemState state)
{
    std::lock_guard lock {mMutex};

    try {
        Poco::Data::Statement statement {*mSession};

        statement << "UPDATE imagemanager SET state = ? WHERE id = ? AND version = ?;", bind(state.ToString().CStr()),
            bind(id.CStr()), bind(version.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
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

        statement << "DELETE FROM imagemanager WHERE id = ? AND version = ?;", bind(id.CStr()), bind(version.CStr());

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error Database::GetItemsInfo(Array<imagemanager::storage::ItemInfo>& items)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<ImageManagerItemInfoRow> rows;

        *mSession << "SELECT id, type, version, state, path, totalSize, gid, timestamp, images FROM imagemanager;",
            into(rows), now;

        auto itemInfo = std::make_unique<imagemanager::storage::ItemInfo>();
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

Error Database::GetItemVersionsByID(const String& id, Array<imagemanager::storage::ItemInfo>& items)
{
    std::lock_guard lock {mMutex};

    try {
        std::vector<ImageManagerItemInfoRow> rows;

        *mSession << "SELECT id, type, version, state, path, totalSize, gid, timestamp, images FROM imagemanager WHERE "
                     "id = ?;",
            bind(id.CStr()), into(rows), now;

        auto itemInfo = std::make_unique<imagemanager::storage::ItemInfo>();
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

Error Database::AddItem(const imagemanager::storage::ItemInfo& item)
{
    std::lock_guard lock {mMutex};

    try {
        ImageManagerItemInfoRow row;

        FromAos(item, row);
        *mSession << "INSERT INTO imagemanager (id, type, version, state, path, totalSize, gid, timestamp, images) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
            bind(row), now;
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
        // Check if network exists
        size_t count = 0;

        *mSession << "SELECT COUNT(*) FROM networks WHERE networkID = ?;", bind(networkID.CStr()), into(count), now;

        if (count == 0) {
            return ErrorEnum::eNotFound;
        }

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
        // Check if network exists
        size_t networkCount = 0;

        *mSession << "SELECT COUNT(*) FROM networks WHERE networkID = ?;", bind(instance.mNetworkID.CStr()),
            into(networkCount), now;

        if (networkCount == 0) {
            return ErrorEnum::eNotFound;
        }

        // Check if host exists
        size_t hostCount = 0;

        *mSession << "SELECT COUNT(*) FROM hosts WHERE networkID = ? AND nodeID = ?;", bind(instance.mNetworkID.CStr()),
            bind(instance.mNodeID.CStr()), into(hostCount), now;

        if (hostCount == 0) {
            return ErrorEnum::eNotFound;
        }

        NetworkManagerInstanceRow row;

        FromAos(instance, row);
        *mSession << "INSERT INTO networkmanager_instances (itemID, subjectID, instance, networkID, nodeID, ip, "
                     "exposedPorts, dnsServers) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
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

        *mSession << "SELECT itemID, subjectID, instance, networkID, nodeID, ip, exposedPorts, dnsServers FROM "
                     "networkmanager_instances WHERE networkID = ? AND nodeID = ?;",
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

        statement << "DELETE FROM networkmanager_instances WHERE itemID = ? AND subjectID = ? AND instance = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance);

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
        *mSession << "INSERT INTO launcher_instances (itemID, subjectID, instance, imageID, updateItemType, nodeID, "
                     "prevNodeID, runtimeID, uid, timestamp, cached) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
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
            << "UPDATE launcher_instances SET imageID = ?, updateItemType = ?, nodeID = ?, prevNodeID = ?, "
               "runtimeID = ?, uid = ?, timestamp = ?, cached = ? WHERE itemID = ? AND subjectID = ? AND instance = ?;",
            bind(info.mImageID.CStr()), bind(info.mUpdateItemType.ToString().CStr()), bind(info.mNodeID.CStr()),
            bind(info.mPrevNodeID.CStr()), bind(info.mRuntimeID.CStr()), bind(info.mUID),
            bind(info.mTimestamp.UnixNano()), bind(info.mCached), bind(info.mInstanceIdent.mItemID.CStr()),
            bind(info.mInstanceIdent.mSubjectID.CStr()), bind(info.mInstanceIdent.mInstance);

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

        *mSession << "SELECT itemID, subjectID, instance, imageID, updateItemType, nodeID, prevNodeID, runtimeID, uid, "
                     "timestamp, cached FROM launcher_instances WHERE itemID = ? AND subjectID = ? AND instance = ?;",
            bind(instanceID.mItemID.CStr()), bind(instanceID.mSubjectID.CStr()), bind(instanceID.mInstance), into(rows),
            now;

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

        *mSession << "SELECT itemID, subjectID, instance, imageID, updateItemType, nodeID, prevNodeID, runtimeID, uid, "
                     "timestamp, cached FROM launcher_instances;",
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
        statement << "DELETE FROM launcher_instances WHERE itemID = ? AND subjectID = ? AND instance = ?;",
            bind(instanceIdent.mItemID.CStr()), bind(instanceIdent.mSubjectID.CStr()), bind(instanceIdent.mInstance);

        if (statement.execute() != 1) {
            return ErrorEnum::eNotFound;
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
                 "storageQuota INTEGER,"
                 "stateQuota INTEGER,"
                 "stateChecksum BLOB,"
                 "PRIMARY KEY(itemID,subjectID,instance)"
                 ");",
        now;

    LOG_INF() << "Create imagemanager table";

    *mSession << "CREATE TABLE IF NOT EXISTS imagemanager ("
                 "id TEXT,"
                 "type TEXT,"
                 "version TEXT,"
                 "state TEXT,"
                 "path TEXT,"
                 "totalSize INTEGER,"
                 "gid INTEGER,"
                 "timestamp INTEGER,"
                 "images TEXT,"
                 "PRIMARY KEY(id,version)"
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
                 "PRIMARY KEY(networkID,nodeID)"
                 ");",
        now;

    LOG_INF() << "Create networkmanager instances table";

    *mSession << "CREATE TABLE IF NOT EXISTS networkmanager_instances ("
                 "itemID TEXT,"
                 "subjectID TEXT,"
                 "instance INTEGER,"
                 "networkID TEXT,"
                 "nodeID TEXT,"
                 "ip TEXT,"
                 "exposedPorts TEXT,"
                 "dnsServers TEXT,"
                 "PRIMARY KEY(itemID,subjectID,instance)"
                 ");",
        now;

    LOG_INF() << "Create launcher instances table";

    *mSession << "CREATE TABLE IF NOT EXISTS launcher_instances ("
                 "itemID TEXT,"
                 "subjectID TEXT,"
                 "instance INTEGER,"
                 "imageID TEXT,"
                 "updateItemType TEXT,"
                 "nodeID TEXT,"
                 "prevNodeID TEXT,"
                 "runtimeID TEXT,"
                 "uid INTEGER,"
                 "timestamp INTEGER,"
                 "cached INTEGER,"
                 "PRIMARY KEY(itemID,subjectID,instance)"
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
    dst.set<ToInt(StorageStateInstanceInfoColumns::eStorageQuota)>(src.mStorageQuota);
    dst.set<ToInt(StorageStateInstanceInfoColumns::eStateQuota)>(src.mStateQuota);
    dst.set<ToInt(StorageStateInstanceInfoColumns::eStateChecksum)>(
        Poco::Data::BLOB(src.mStateChecksum.begin(), src.mStateChecksum.Size()));
}

void Database::ToAos(const StorageStateInstanceInfoRow& src, storagestate::InstanceInfo& dst)
{
    const auto& blob = src.get<ToInt(StorageStateInstanceInfoColumns::eStateChecksum)>();

    dst.mInstanceIdent.mItemID    = src.get<ToInt(StorageStateInstanceInfoColumns::eItemID)>().c_str();
    dst.mInstanceIdent.mSubjectID = src.get<ToInt(StorageStateInstanceInfoColumns::eSubjectID)>().c_str();
    dst.mInstanceIdent.mInstance  = src.get<ToInt(StorageStateInstanceInfoColumns::eInstance)>();
    dst.mStorageQuota             = src.get<ToInt(StorageStateInstanceInfoColumns::eStorageQuota)>();
    dst.mStateQuota               = src.get<ToInt(StorageStateInstanceInfoColumns::eStateQuota)>();
    AOS_ERROR_CHECK_AND_THROW(dst.mStateChecksum.Assign(Array<uint8_t>(blob.rawContent(), blob.size())));
}

void Database::FromAos(const imagemanager::storage::ItemInfo& src, ImageManagerItemInfoRow& dst)
{
    dst.set<ToInt(ImageManagerItemInfoColumns::eID)>(src.mID.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eType)>(src.mType.ToString().CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eVersion)>(src.mVersion.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eState)>(src.mState.ToString().CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::ePath)>(src.mPath.CStr());
    dst.set<ToInt(ImageManagerItemInfoColumns::eTotalSize)>(src.mTotalSize);
    dst.set<ToInt(ImageManagerItemInfoColumns::eGID)>(src.mGID);
    dst.set<ToInt(ImageManagerItemInfoColumns::eTimestamp)>(src.mTimestamp.UnixNano());
    dst.set<ToInt(ImageManagerItemInfoColumns::eImages)>(SerializeImages(src.mImages));
}

void Database::ToAos(const ImageManagerItemInfoRow& src, imagemanager::storage::ItemInfo& dst)
{
    dst.mID        = src.get<ToInt(ImageManagerItemInfoColumns::eID)>().c_str();
    dst.mVersion   = src.get<ToInt(ImageManagerItemInfoColumns::eVersion)>().c_str();
    dst.mPath      = src.get<ToInt(ImageManagerItemInfoColumns::ePath)>().c_str();
    dst.mTotalSize = src.get<ToInt(ImageManagerItemInfoColumns::eTotalSize)>();
    dst.mGID       = src.get<ToInt(ImageManagerItemInfoColumns::eGID)>();
    dst.mTimestamp
        = Time::Unix(src.get<ToInt(ImageManagerItemInfoColumns::eTimestamp)>() / Time::cSeconds.Nanoseconds(),
            src.get<ToInt(ImageManagerItemInfoColumns::eTimestamp)>() % Time::cSeconds.Nanoseconds());

    if (auto err = dst.mType.FromString(src.get<ToInt(ImageManagerItemInfoColumns::eType)>().c_str()); !err.IsNone()) {
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse item type");
    }

    if (auto err = dst.mState.FromString(src.get<ToInt(ImageManagerItemInfoColumns::eState)>().c_str());
        !err.IsNone()) {
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse item state");
    }

    DeserializeImages(src.get<ToInt(ImageManagerItemInfoColumns::eImages)>(), dst.mImages);
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
    dst.mNetworkID                = src.get<ToInt(NetworkManagerInstanceColumns::eNetworkID)>().c_str();
    dst.mNodeID                   = src.get<ToInt(NetworkManagerInstanceColumns::eNodeID)>().c_str();
    dst.mIP                       = src.get<ToInt(NetworkManagerInstanceColumns::eIP)>().c_str();

    DeserializeExposedPorts(src.get<ToInt(NetworkManagerInstanceColumns::eExposedPorts)>(), dst.mExposedPorts);
    DeserializeDNSServers(src.get<ToInt(NetworkManagerInstanceColumns::eDNSServers)>(), dst.mDNSServers);
}

void Database::FromAos(const launcher::InstanceInfo& src, LauncherInstanceInfoRow& dst)
{
    dst.set<ToInt(LauncherInstanceInfoColumns::eItemID)>(src.mInstanceIdent.mItemID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eSubjectID)>(src.mInstanceIdent.mSubjectID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eInstance)>(src.mInstanceIdent.mInstance);
    dst.set<ToInt(LauncherInstanceInfoColumns::eImageID)>(src.mImageID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eUpdateItemType)>(src.mUpdateItemType.ToString().CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eNodeID)>(src.mNodeID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::ePrevNodeID)>(src.mPrevNodeID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eRuntimeID)>(src.mRuntimeID.CStr());
    dst.set<ToInt(LauncherInstanceInfoColumns::eUID)>(src.mUID);
    dst.set<ToInt(LauncherInstanceInfoColumns::eTimestamp)>(src.mTimestamp.UnixNano());
    dst.set<ToInt(LauncherInstanceInfoColumns::eCached)>(src.mCached);
}

void Database::ToAos(const LauncherInstanceInfoRow& src, launcher::InstanceInfo& dst)
{
    auto timestamp      = src.get<ToInt(LauncherInstanceInfoColumns::eTimestamp)>();
    auto updateItemType = src.get<ToInt(LauncherInstanceInfoColumns::eUpdateItemType)>();

    dst.mInstanceIdent.mItemID    = src.get<ToInt(LauncherInstanceInfoColumns::eItemID)>().c_str();
    dst.mInstanceIdent.mSubjectID = src.get<ToInt(LauncherInstanceInfoColumns::eSubjectID)>().c_str();
    dst.mInstanceIdent.mInstance  = src.get<ToInt(LauncherInstanceInfoColumns::eInstance)>();
    dst.mImageID                  = src.get<ToInt(LauncherInstanceInfoColumns::eImageID)>().c_str();
    dst.mNodeID                   = src.get<ToInt(LauncherInstanceInfoColumns::eNodeID)>().c_str();
    dst.mPrevNodeID               = src.get<ToInt(LauncherInstanceInfoColumns::ePrevNodeID)>().c_str();
    dst.mRuntimeID                = src.get<ToInt(LauncherInstanceInfoColumns::eRuntimeID)>().c_str();
    dst.mUID                      = src.get<ToInt(LauncherInstanceInfoColumns::eUID)>();
    dst.mTimestamp = Time::Unix(timestamp / Time::cSeconds.Nanoseconds(), timestamp % Time::cSeconds.Nanoseconds());
    dst.mCached    = src.get<ToInt(LauncherInstanceInfoColumns::eCached)>();

    AOS_ERROR_CHECK_AND_THROW(
        dst.mUpdateItemType.FromString(updateItemType.c_str()), "failed to parse update item type");
}

} // namespace aos::cm::database
