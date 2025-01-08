/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <memory>
#include <sys/xattr.h>
#include <vector>

#include <aos/sm/image/imageparts.hpp>
#include <utils/filesystem.hpp>
#include <utils/image.hpp>

#include "imagehandler.hpp"
#include "logger/logmodule.hpp"

namespace aos::sm::image {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cSHA256Prefix      = "sha256:";
constexpr auto cWhiteoutPrefix    = ".wh.";
constexpr auto cWhiteoutOpaqueDir = ".wh..wh..opq";
constexpr auto cBlobsFolder       = "blobs";
constexpr auto cManifestFile      = "manifest.json";
constexpr auto cTmpRootFSDir      = "tmprootfs";
const int      cBufferSize        = 1024 * 1024;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Error OCIWhiteoutsToOverlay(const String& path, uint32_t uid, uint32_t gid)
{
    LOG_DBG() << "Converting OCI whiteouts to overlayfs: path=" << path;

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path.CStr())) {
            const std::string baseName = entry.path().filename().string();
            const std::string dirName  = entry.path().parent_path().string();

            if (entry.is_directory()) {
                continue;
            }

            if (baseName == cWhiteoutOpaqueDir) {
                if (auto res = setxattr(dirName.c_str(), "trusted.overlay.opaque", "y", 1, 0); res != 0) {
                    return AOS_ERROR_WRAP(res);
                }

                continue;
            }

            if (baseName.find(cWhiteoutPrefix) == 0) {
                auto fullPath = FS::JoinPath(dirName.c_str(), baseName.substr(strlen(cWhiteoutPrefix)).c_str());

                if (auto res = mknod(fullPath.CStr(), S_IFCHR, 0); res != 0) {
                    return AOS_ERROR_WRAP(res);
                }

                if (auto res = chown(fullPath.CStr(), uid, gid); res != 0) {
                    return AOS_ERROR_WRAP(res);
                }

                continue;
            }
        }
    } catch (const std::exception& e) {
        throw Error(ErrorEnum::eFailed, e.what());
    }

    return ErrorEnum::eNone;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ImageHandler::Init(crypto::HasherItf& hasher, spaceallocator::SpaceAllocatorItf& layerSpaceAllocator,
    spaceallocator::SpaceAllocatorItf& serviceSpaceAllocator, oci::OCISpecItf& ociSpec, uint32_t uid)
{
    LOG_DBG() << "Initialize image handler";

    mHasher                = &hasher;
    mLayerSpaceAllocator   = &layerSpaceAllocator;
    mServiceSpaceAllocator = &serviceSpaceAllocator;
    mOCISpec               = &ociSpec;
    mUID                   = uid;

    return ErrorEnum::eNone;
}

RetWithError<StaticString<cFilePathLen>> ImageHandler::InstallLayer(const String& archivePath,
    const String& installBasePath, const LayerInfo& layer, UniquePtr<aos::spaceallocator::SpaceItf>& space)
{
    auto err = CheckFileInfo(archivePath, layer.mSize, layer.mSHA256);

    if (!err.IsNone()) {
        return {{}, err};
    }

    size_t      archiveSize = 0;
    std::string extractDir;

    if (Tie(extractDir, err) = common::utils::MkTmpDir(installBasePath.CStr()); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(Error(err, "failed to create temporary extract dir"))};
    }

    if (Tie(archiveSize, err) = common::utils::GetUnpackedArchiveSize(archivePath.CStr()); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    if (Tie(space, err) = mLayerSpaceAllocator->AllocateSpace(archiveSize); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    auto cleanExtractDir = DeferRelease(&err, [&, extractSize = space->Size()](Error*) {
        if (!extractDir.empty()) {
            FS::RemoveAll(extractDir.c_str());
        }

        assert(space.Get() != nullptr);
        assert(space->Size() >= extractSize);

        space->Resize(space->Size() - extractSize);
    });

    if (err = UnpackArchive(archivePath, extractDir.c_str()); !err.IsNone()) {
        return {{}, err};
    }

    auto manifest = std::make_unique<oci::ImageManifest>();

    if (err = mOCISpec->LoadImageManifest(FS::JoinPath(extractDir.c_str(), cManifestFile), *manifest); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(Error(err, "failed to load image manifest"))};
    }

    const auto parsedDigest = common::utils::ParseDigest(manifest->mConfig.mDigest.CStr());
    const auto installDir   = FS::JoinPath(installBasePath, parsedDigest.first.c_str(), parsedDigest.second.c_str());
    const auto embeddedArchivePath = FS::JoinPath(extractDir.c_str(), parsedDigest.second.c_str());

    if (Tie(archiveSize, err) = common::utils::GetUnpackedArchiveSize(embeddedArchivePath.CStr()); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    if (err = space->Resize(space->Size() + archiveSize); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    if (err = UnpackArchive(embeddedArchivePath, installDir); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(Error(err, "failed to unpack layer's embedded archive"))};
    }

    if (err = OCIWhiteoutsToOverlay(String(extractDir.c_str(), extractDir.size()), 0, 0); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(Error(err, "failed to convert OCI whiteouts to overlay"))};
    }

    LOG_DBG() << "Layer has been successfully installed: src=" << archivePath << ", dst=" << installDir
              << ", size=" << space->Size();

    return {installDir, ErrorEnum::eNone};
}

RetWithError<StaticString<cFilePathLen>> ImageHandler::InstallService(const String& archivePath,
    const String& installBasePath, const ServiceInfo& service, UniquePtr<aos::spaceallocator::SpaceItf>& space)
{
    LOG_DBG() << "Install service: archive=" << archivePath << ", installBasePath=" << installBasePath
              << ", serviceID=" << service.mServiceID;

    if (auto err = CheckFileInfo(archivePath, service.mSize, service.mSHA256); !err.IsNone()) {
        return {{}, err};
    }

    size_t unpackedSize = 0;
    Error  err;

    if (err = FS::MakeDirAll(installBasePath); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(Error(err, "failed to create installation base dir"))};
    }

    std::string installDir;

    if (Tie(installDir, err) = common::utils::MkTmpDir(installBasePath.CStr()); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(Error(err, "failed to create temporary installation dir"))};
    }

    if (Tie(unpackedSize, err) = common::utils::GetUnpackedArchiveSize(archivePath.CStr()); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    if (Tie(space, err) = mServiceSpaceAllocator->AllocateSpace(unpackedSize); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    if (err = UnpackArchive(archivePath, installDir.c_str()); !err.IsNone()) {
        return {{}, err};
    }

    auto manifest = std::make_unique<oci::ImageManifest>();

    if (err = mOCISpec->LoadImageManifest(FS::JoinPath(installDir.c_str(), "manifest.json"), *manifest);
        !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(Error(err, "failed to load image manifest"))};
    }

    if (err = ValidateService(installDir.c_str(), *manifest); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    if (err = PrepareServiceFS(installDir.c_str(), service, *manifest, space); !err.IsNone()) {
        return {{}, err};
    }

    LOG_DBG() << "Service has been successfully installed: src=" << archivePath << ", dst=" << installDir.c_str()
              << ", size=" << space->Size();

    return {installDir.c_str(), ErrorEnum::eNone};
}

Error ImageHandler::ValidateService(const String& path) const
{
    auto imageManifest = std::make_unique<oci::ImageManifest>();

    if (auto err = mOCISpec->LoadImageManifest(FS::JoinPath(path, "manifest.json"), *imageManifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to load image manifest"));
    }

    if (auto err = ValidateService(path, *imageManifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

RetWithError<StaticString<oci::cMaxDigestLen>> ImageHandler::CalculateDigest(const String& path) const
{
    auto [hash, err] = CalculateHash(path, crypto::HashEnum::eSHA256);
    if (!err.IsNone()) {
        return {{}, err};
    }

    StaticString<oci::cMaxDigestLen> digestStr;
    if (err = digestStr.ByteArrayToHex(hash); !err.IsNone()) {
        return {{}, err};
    }

    LOG_DBG() << "Calculated digest: path=" << path << ", digest=" << digestStr;

    return {digestStr, ErrorEnum::eNone};
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error ImageHandler::ValidateServiceConfig(const String& path, const String& digest) const
{
    const auto parsedDigest = common::utils::ParseDigest(digest.CStr());
    const auto serviceConfigPath
        = FS::JoinPath(path, cBlobsFolder, parsedDigest.first.c_str(), parsedDigest.second.c_str());

    auto serviceConfig = std::make_unique<oci::ServiceConfig>();

    if (auto err = mOCISpec->LoadServiceConfig(serviceConfigPath, *serviceConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to load service config"));
    }

    return ErrorEnum::eNone;
}

Error ImageHandler::ValidateService(const String& path, const oci::ImageManifest& manifest) const
{
    if (auto err = ValidateDigest(path, manifest.mConfig.mDigest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (manifest.mAosService.HasValue()) {
        if (auto err = ValidateDigest(path, manifest.mAosService->mDigest); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = ValidateServiceConfig(path, manifest.mAosService->mDigest); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (manifest.mLayers.Size() == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "no layers found"));
    }

    if (auto err = ValidateDigest(path, manifest.mLayers[0].mDigest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error ImageHandler::ValidateDigest(const String& path, const String& digest) const
{
    const auto parsedDigest = common::utils::ParseDigest(digest.CStr());
    const auto fullPath     = FS::JoinPath(path, cBlobsFolder, parsedDigest.first.c_str(), parsedDigest.second.c_str());

    std::error_code ec;
    if (!std::filesystem::exists(fullPath.CStr()) || ec.value() != 0) {
        LOG_ERR() << "Failed to validate digest: path=" << fullPath << ", err=" << ec.message().c_str();

        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, ec.message().c_str()));
    }

    StaticString<oci::cMaxDigestLen> calculatedDigest;

    if (std::filesystem::is_directory(fullPath.CStr())) {
        auto [hash, err] = common::utils::HashDir(fullPath.CStr());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        calculatedDigest = hash.c_str();

        return (calculatedDigest == digest) ? ErrorEnum::eNone : ErrorEnum::eInvalidChecksum;
    }

    auto [sha256, err] = CalculateHash(fullPath, crypto::HashEnum::eSHA256);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = calculatedDigest.ByteArrayToHex(sha256); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return (calculatedDigest.Prepend(cSHA256Prefix) == digest) ? ErrorEnum::eNone : ErrorEnum::eInvalidChecksum;
}

Error ImageHandler::CheckFileInfo(const String& path, uint64_t size, const Array<uint8_t>& sha256) const
{
    std::error_code ec;
    const auto      actualSize = std::filesystem::file_size(path.CStr(), ec);

    if (ec.value() != 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, ec.message().c_str()));
    }

    if (size != actualSize) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "file size mismatch"));
    }

    auto [calculatedSHA256, err] = CalculateHash(path, crypto::HashEnum::eSHA3_256);
    if (!err.IsNone()) {
        return err;
    }

    return (sha256 == calculatedSHA256) ? ErrorEnum::eNone : ErrorEnum::eInvalidChecksum;
}

RetWithError<StaticArray<uint8_t, cSHA256Size>> ImageHandler::CalculateHash(
    const String& path, crypto::Hash algorithm) const
{
    std::ifstream file(path.CStr(), std::ios::binary);
    if (!file.is_open()) {
        return {{}, AOS_ERROR_WRAP(ErrorEnum::eNotFound)};
    }

    auto [hasher, err] = mHasher->CreateHash(algorithm);
    if (!err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    std::vector<uint8_t> buffer(cBufferSize, 0);

    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

        if (const auto bytesRead = file.gcount(); bytesRead > 0) {
            if (err = hasher->Update(Array<uint8_t>(buffer.data(), static_cast<size_t>(bytesRead))); !err.IsNone()) {
                return {{}, AOS_ERROR_WRAP(Error(err, "failed to calculate hash"))};
            }
        }
    }

    StaticArray<uint8_t, cSHA256Size> hash;

    if (err = hasher->Finalize(hash); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(Error(err, "failed to calculate hash"))};
    }

    return {hash, ErrorEnum::eNone};
}

Error ImageHandler::UnpackArchive(const String& source, const String& destination) const
{
    LOG_DBG() << "Unpack archive: source=" << source << ", destination=" << destination;

    if (auto err = FS::MakeDirAll(destination); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = common::utils::UnpackTarImage(source.CStr(), destination.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error ImageHandler::PrepareServiceFS(const String& baseDir, const ServiceInfo& service, oci::ImageManifest& manifest,
    UniquePtr<aos::spaceallocator::SpaceItf>& space) const
{
    LOG_DBG() << "Preparing service rootfs: baseDir=" << baseDir << ", service=" << service.mServiceID;

    auto imageParts = std::make_unique<ImageParts>();

    Error err = GetImagePartsFromManifest(manifest, *imageParts);

    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to get image parts"));
    }

    const auto rootFSArchive = FS::JoinPath(baseDir.CStr(), cBlobsFolder, imageParts->mServiceFSPath);
    const auto tmpRootFS     = FS::JoinPath(baseDir, cTmpRootFSDir);
    size_t     archiveSize   = 0;
    size_t     unpackedSize  = 0;

    if (Tie(archiveSize, err) = common::utils::CalculateSize(rootFSArchive.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (Tie(unpackedSize, err) = common::utils::GetUnpackedArchiveSize(rootFSArchive.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = space->Resize(space->Size() + unpackedSize); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = UnpackArchive(rootFSArchive, tmpRootFS.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = FS::RemoveAll(rootFSArchive); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to remove origin rootfs"));
    }

    if (err = space->Resize(space->Size() - archiveSize); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = common::utils::ChangeOwner(tmpRootFS.CStr(), mUID, service.mGID); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to change service rootfs owner"));
    }

    if (err = OCIWhiteoutsToOverlay(tmpRootFS, mUID, service.mGID); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to convert OCI whiteouts to overlay"));
    }

    std::string rootFSHash;
    if (Tie(rootFSHash, err) = common::utils::HashDir(tmpRootFS.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to hash service rootfs directory"));
    }

    const auto [algorithm, hash] = common::utils::ParseDigest(rootFSHash);
    const auto installPath       = FS::JoinPath(baseDir, cBlobsFolder, algorithm.c_str(), hash.c_str());

    std::error_code ec;

    std::filesystem::rename(tmpRootFS.CStr(), installPath.CStr(), ec);
    if (ec.value() != 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, ec.message().c_str()));
    }

    manifest.mLayers[0].mDigest = rootFSHash.c_str();

    if (err = mOCISpec->SaveImageManifest(FS::JoinPath(baseDir, cManifestFile), manifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to save image manifest"));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::image
