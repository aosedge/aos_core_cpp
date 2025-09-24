/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <vector>

#include <core/sm/image/imageparts.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>
#include <common/utils/image.hpp>
#include <sm/logger/logmodule.hpp>

#include "imagehandler.hpp"

namespace aos::sm::image {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cSHA256Prefix        = "sha256:";
constexpr auto cWhiteoutPrefix      = ".wh.";
constexpr auto cWhiteoutOpaqueDir   = ".wh..wh..opq";
constexpr auto cBlobsFolder         = "blobs";
constexpr auto cLayerManifestFile   = "layer.json";
constexpr auto cServiceManifestFile = "manifest.json";
constexpr auto cTmpRootFSDir        = "tmprootfs";
const int      cBufferSize          = 1024 * 1024;

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

            if (baseName.rfind(cWhiteoutPrefix, 0) == 0) {
                auto fullPath = std::filesystem::path(dirName) / baseName.substr(strlen(cWhiteoutPrefix));

                if (auto res = mknod(fullPath.c_str(), S_IFCHR, 0); res != 0) {
                    return AOS_ERROR_WRAP(res);
                }

                if (auto res = chown(fullPath.c_str(), uid, gid); res != 0) {
                    return AOS_ERROR_WRAP(res);
                }

                continue;
            }
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

std::set<std::filesystem::path> GetAllFilesByPath(const std::filesystem::path& path)
{
    std::set<std::filesystem::path> files;

    if (std::filesystem::is_regular_file(path)) {
        files.insert(path);

        return files;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            files.insert(entry.path());
        }
    }

    return files;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ImageHandler::Init(crypto::HasherItf& hasher, spaceallocator::SpaceAllocatorItf& layerSpaceAllocator,
    spaceallocator::SpaceAllocatorItf& serviceSpaceAllocator, oci::OCISpecItf& ociSpec, fs::FSPlatformItf& fsPlatform,
    uint32_t uid)
{
    LOG_DBG() << "Init image handler";

    mHasher                = &hasher;
    mLayerSpaceAllocator   = &layerSpaceAllocator;
    mServiceSpaceAllocator = &serviceSpaceAllocator;
    mOCISpec               = &ociSpec;
    mFSPlatform            = &fsPlatform;
    mUID                   = uid;

    return ErrorEnum::eNone;
}

RetWithError<StaticString<cFilePathLen>> ImageHandler::InstallLayer(const String& archivePath,
    const String& installBasePath, const LayerInfo& layer, UniquePtr<aos::spaceallocator::SpaceItf>& space)
{
    LOG_DBG() << "Install layer: archive=" << archivePath << ", digest=" << layer.mLayerDigest;

    RetWithError<StaticString<cFilePathLen>> result("");

    auto err = CheckFileInfo(archivePath, layer.mSize, layer.mSHA256);
    if (!err.IsNone()) {
        result.mError = err;
        return result;
    }

    size_t      archiveSize = 0;
    std::string extractDir;

    if (Tie(extractDir, err) = common::utils::MkTmpDir(installBasePath.CStr()); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(Error(err, "failed to create temporary extract dir"));
        return result;
    }

    if (Tie(archiveSize, err) = common::utils::GetUnpackedArchiveSize(archivePath.CStr()); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(err);
        return result;
    }

    if (Tie(space, err) = mLayerSpaceAllocator->AllocateSpace(archiveSize); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(err);
        return result;
    }

    auto cleanExtractDir = DeferRelease(&err, [&, extractSize = space->Size()](Error*) {
        if (!extractDir.empty()) {
            std::filesystem::remove_all(extractDir);
        }

        assert(space.Get() != nullptr);
        assert(space->Size() >= extractSize);

        space->Resize(space->Size() - extractSize);
    });

    if (err = UnpackArchive(archivePath, extractDir.c_str()); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(err);
        return result;
    }

    auto contentDescriptor = std::make_unique<oci::ContentDescriptor>();
    auto manifestPath      = std::filesystem::path(extractDir) / cLayerManifestFile;

    if (err = mOCISpec->LoadContentDescriptor(manifestPath.c_str(), *contentDescriptor); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(Error(err, "failed to load content descriptor"));
        return result;
    }

    const auto parsedDigest = common::utils::ParseDigest(contentDescriptor->mDigest.CStr());
    const auto installDir   = std::filesystem::path(installBasePath.CStr()) / parsedDigest.first / parsedDigest.second;
    const auto embeddedArchivePath = std::filesystem::path(extractDir) / parsedDigest.second;

    if (Tie(archiveSize, err) = common::utils::GetUnpackedArchiveSize(embeddedArchivePath, false); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(err);
        return result;
    }

    if (err = space->Resize(space->Size() + archiveSize); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(err);
        return result;
    }

    if (err = UnpackArchive(embeddedArchivePath.c_str(), installDir.c_str()); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(Error(err, "failed to unpack layer's embedded archive"));
        return result;
    }

    if (err = OCIWhiteoutsToOverlay(extractDir.c_str(), 0, 0); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(Error(err, "failed to convert OCI whiteouts to overlay"));
        return result;
    }

    LOG_DBG() << "Layer has been successfully installed: path=" << installDir.c_str();

    result.mValue = installDir.c_str();

    return result;
}

RetWithError<StaticString<cFilePathLen>> ImageHandler::InstallService(const String& archivePath,
    const String& installBasePath, const ServiceInfo& service, UniquePtr<aos::spaceallocator::SpaceItf>& space)
{
    LOG_DBG() << "Install service: archive=" << archivePath << ", installBasePath=" << installBasePath
              << ", serviceID=" << service.mServiceID;

    RetWithError<StaticString<cFilePathLen>> result("");

    if (auto err = CheckFileInfo(archivePath, service.mSize, service.mSHA256); !err.IsNone()) {
        result.mError = err;
        return result;
    }

    size_t unpackedSize = 0;

    auto installDir = std::filesystem::path(installBasePath.CStr())
        / (std::string(service.mServiceID.CStr()) + "-v" + service.mVersion.CStr());

    if (auto [exists, err] = fs::DirExist(installDir.c_str()); !err.IsNone() || exists) {
        result.mError = AOS_ERROR_WRAP(Error(ErrorEnum::eAlreadyExist, "service already exists"));
        return result;
    }

    if (auto err = fs::MakeDirAll(installDir.c_str()); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(Error(err, "failed to create service installation dir"));
        return result;
    }

    Error err = ErrorEnum::eNone;

    auto cleanInstallDir = DeferRelease(&err, [installDir](const Error* err) {
        if (!err->IsNone()) {
            std::filesystem::remove_all(installDir);
        }
    });

    if (Tie(unpackedSize, err) = common::utils::GetUnpackedArchiveSize(archivePath.CStr()); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(err);
        return result;
    }

    if (Tie(space, err) = mServiceSpaceAllocator->AllocateSpace(unpackedSize); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(err);
        return result;
    }

    if (err = UnpackArchive(archivePath, installDir.c_str()); !err.IsNone()) {
        result.mError = err;
        return result;
    }

    auto manifest     = std::make_unique<oci::ImageManifest>();
    auto manifestPath = std::filesystem::path(installDir) / cServiceManifestFile;

    if (err = mOCISpec->LoadImageManifest(manifestPath.c_str(), *manifest); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(Error(err, "failed to load image manifest"));
        return result;
    }

    if (err = ValidateService(installDir.c_str(), *manifest); !err.IsNone()) {
        result.mError = AOS_ERROR_WRAP(err);
        return result;
    }

    if (err = PrepareServiceFS(installDir.c_str(), service, *manifest, space); !err.IsNone()) {
        result.mError = err;
        return result;
    }
    LOG_DBG() << "Service has been successfully installed: src=" << archivePath << ", dst=" << installDir.c_str()
              << ", size=" << space->Size();

    result.mValue = installDir.c_str();

    return result;
}

Error ImageHandler::ValidateService(const String& path) const
{
    auto imageManifest = std::make_unique<oci::ImageManifest>();
    auto manifestPath  = std::filesystem::path(path.CStr()) / cServiceManifestFile;

    if (auto err = mOCISpec->LoadImageManifest(manifestPath.c_str(), *imageManifest); !err.IsNone()) {
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
        = std::filesystem::path(path.CStr()) / cBlobsFolder / parsedDigest.first / parsedDigest.second;

    auto serviceConfig = std::make_unique<oci::ServiceConfig>();

    if (auto err = mOCISpec->LoadServiceConfig(serviceConfigPath.c_str(), *serviceConfig); !err.IsNone()) {
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
    const auto fullPath = std::filesystem::path(path.CStr()) / cBlobsFolder / parsedDigest.first / parsedDigest.second;

    std::error_code ec;
    if (!std::filesystem::exists(fullPath) || ec.value() != 0) {
        LOG_ERR() << "Failed to validate digest: path=" << fullPath.c_str() << ", err=" << ec.message().c_str();

        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, ec.message().c_str()));
    }

    StaticString<oci::cMaxDigestLen> calculatedDigest;

    if (std::filesystem::is_directory(fullPath)) {
        auto [hash, err] = common::utils::HashDir(fullPath);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        calculatedDigest = hash.c_str();

        return (calculatedDigest == digest) ? ErrorEnum::eNone : ErrorEnum::eInvalidChecksum;
    }

    auto [sha256, err] = CalculateHash(fullPath.c_str(), crypto::HashEnum::eSHA256);
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
    StaticArray<uint8_t, cSHA256Size> hash;

    try {
        auto [hasher, err] = mHasher->CreateHash(algorithm);
        if (!err.IsNone()) {
            return {{}, AOS_ERROR_WRAP(err)};
        }

        for (const auto& file : GetAllFilesByPath(path.CStr())) {
            std::ifstream ifile(file, std::ios::binary);
            if (!ifile.is_open()) {
                return {{}, AOS_ERROR_WRAP(ErrorEnum::eNotFound)};
            }

            std::vector<uint8_t> buffer(cBufferSize, 0);

            while (ifile) {
                ifile.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

                if (const auto bytesRead = ifile.gcount(); bytesRead > 0) {
                    if (err = hasher->Update(Array<uint8_t>(buffer.data(), static_cast<size_t>(bytesRead)));
                        !err.IsNone()) {
                        return {{}, AOS_ERROR_WRAP(Error(err, "failed to calculate hash"))};
                    }
                }
            }
        }

        if (err = hasher->Finalize(hash); !err.IsNone()) {
            return {{}, AOS_ERROR_WRAP(Error(err, "failed to calculate hash"))};
        }
    } catch (const std::exception& e) {
        return {{}, AOS_ERROR_WRAP(common::utils::ToAosError(e))};
    }

    return {hash, ErrorEnum::eNone};
}

Error ImageHandler::UnpackArchive(const String& source, const String& destination) const
{
    LOG_DBG() << "Unpack archive: source=" << source << ", destination=" << destination;

    if (auto err = fs::MakeDirAll(destination); !err.IsNone()) {
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

    const auto rootFSArchive = std::filesystem::path(baseDir.CStr()) / cBlobsFolder / imageParts->mServiceFSPath.CStr();
    const auto tmpRootFS     = std::filesystem::path(baseDir.CStr()) / cTmpRootFSDir;
    size_t     archiveSize   = 0;
    size_t     unpackedSize  = 0;

    if (Tie(archiveSize, err) = common::utils::CalculateSize(rootFSArchive); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (Tie(unpackedSize, err) = common::utils::GetUnpackedArchiveSize(rootFSArchive); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = space->Resize(space->Size() + unpackedSize); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = UnpackArchive(rootFSArchive.c_str(), tmpRootFS.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::filesystem::remove_all(rootFSArchive);

    if (err = space->Resize(space->Size() - archiveSize); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mFSPlatform->ChangeOwner(tmpRootFS.c_str(), mUID, service.mGID); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to change service rootfs owner"));
    }

    if (err = OCIWhiteoutsToOverlay(tmpRootFS.c_str(), mUID, service.mGID); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to convert OCI whiteouts to overlay"));
    }

    std::string rootFSHash;
    if (Tie(rootFSHash, err) = common::utils::HashDir(tmpRootFS.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to hash service rootfs directory"));
    }

    const auto [algorithm, hash] = common::utils::ParseDigest(rootFSHash);
    const auto installPath       = std::filesystem::path(baseDir.CStr()) / cBlobsFolder / algorithm / hash;

    std::error_code ec;

    std::filesystem::rename(tmpRootFS, installPath, ec);
    if (ec.value() != 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, ec.message().c_str()));
    }

    manifest.mLayers[0].mDigest = rootFSHash.c_str();
    auto manifestPath           = std::filesystem::path(baseDir.CStr()) / cServiceManifestFile;

    if (err = mOCISpec->SaveImageManifest(manifestPath.c_str(), manifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to save image manifest"));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::image
