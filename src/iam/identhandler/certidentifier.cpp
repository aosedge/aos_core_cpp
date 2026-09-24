/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/crypto/cryptoutils.hpp>
#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>

#include "certidentifier.hpp"

namespace aos::iam::identhandler {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CertIdentifier::Init(const CertIdentifierConfig& config, iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, AllocatorItf& allocator)
{
    LOG_DBG() << "Init cert identifier";

    mConfig       = config;
    mCertProvider = &certProvider;
    mCertLoader   = &certLoader;
    mAllocator    = &allocator;
    mSubjects.Clear();
    mSystemInfo.mSystemID.Clear();

    if (auto err = ReadUnitModel(); !err.IsNone()) {
        return err;
    }

    if (auto err = ReadSubjects(); !err.IsNone()) {
        LOG_WRN() << "Can't read subjects: err=" << err << ". Empty subjects will be used";

        mSubjects.Clear();
    }

    return ErrorEnum::eNone;
}

Error CertIdentifier::Start()
{
    LOG_DBG() << "Start cert identifier";

    if (auto err = UpdateSystemIDFromCert(); !err.IsNone()) {
        LOG_WRN() << "System ID is not available yet" << Log::Field(err);
    }

    if (auto err = mCertProvider->SubscribeListener(cOnlineCert, *this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error CertIdentifier::Stop()
{
    LOG_DBG() << "Stop cert identifier";

    if (auto err = mCertProvider->UnsubscribeListener(*this); !err.IsNone()) {
        LOG_WRN() << "Failed to unsubscribe from online cert changes" << Log::Field(err);
    }

    return ErrorEnum::eNone;
}

Error CertIdentifier::GetSystemInfo(SystemInfo& info)
{
    StaticString<cIDLen> systemID;

    {
        LockGuard lock {mMutex};

        info     = mSystemInfo;
        systemID = mSystemInfo.mSystemID;
    }

    if (systemID.IsEmpty()) {
        if (auto err = UpdateSystemIDFromCert(); !err.IsNone()) {
            LOG_WRN() << "System ID is not available yet" << Log::Field(err);
        } else {
            LockGuard lock {mMutex};

            systemID = mSystemInfo.mSystemID;
        }
    }

    if (auto err = info.mSystemID.Assign(systemID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Get system info" << Log::Field("systemID", info.mSystemID) << Log::Field("unitModel", info.mUnitModel)
              << Log::Field("version", info.mVersion);

    return ErrorEnum::eNone;
}

Error CertIdentifier::GetSubjects(Array<StaticString<cIDLen>>& subjects)
{
    LOG_DBG() << "Get subjects";

    if (subjects.MaxSize() < mSubjects.Size()) {
        return AOS_ERROR_WRAP(ErrorEnum::eNoMemory);
    }

    subjects = mSubjects;

    return ErrorEnum::eNone;
}

void CertIdentifier::OnCertChanged(const CertInfo& /*info*/)
{
    LOG_DBG() << "Online certificate changed, updating System ID";

    if (auto err = UpdateSystemIDFromCert(); !err.IsNone()) {
        LOG_ERR() << "Failed to update System ID from online certificate" << Log::Field(err);
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error CertIdentifier::ReadUnitModel()
{
    StaticString<cUnitModelLen + cVersionLen + 1>                 buffer;
    StaticArray<StaticString<Max(cUnitModelLen, cVersionLen)>, 2> parts;

    if (auto err = fs::ReadFileToString(mConfig.mUnitModelPath, buffer); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = buffer.Split(parts, cModelVersionDelimiter); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (parts.Size() != 2) {
        return AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument);
    }

    if (auto err = mSystemInfo.mUnitModel.Assign(parts[0]); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mSystemInfo.mVersion.Assign(parts[1]); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mSystemInfo.mUnitModel.Trim(cWhiteSpaces);
    mSystemInfo.mVersion.Trim(cWhiteSpaces);

    return ErrorEnum::eNone;
}

Error CertIdentifier::ReadSubjects()
{
    StaticString<cMaxNumSubjects * cIDLen> buffer;

    auto err = fs::ReadFileToString(mConfig.mSubjectsPath, buffer);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = buffer.Split(mSubjects, '\n');
    if (!err.IsNone()) {
        mSubjects.Clear();

        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error CertIdentifier::UpdateSystemIDFromCert()
{
    auto certInfo = MakeUnique<CertInfo>(mAllocator);
    if (!certInfo) {
        return AOS_ERROR_WRAP(ErrorEnum::eNoMemory);
    }

    if (auto err = mCertProvider->GetCert(cOnlineCert, {}, {}, *certInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto [chain, err] = mCertLoader->LoadCertsChainByURL(certInfo->mCertURL);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (!chain || chain->IsEmpty()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "online certificate chain is empty"));
    }

    StaticString<cIDLen> systemID;
    Error                extractErr = ErrorEnum::eNotFound;

    for (const auto& uri : (*chain)[0].mSubjectURLs) {
        extractErr = crypto::GetSystemIDFromCert(uri, systemID);
        if (extractErr.IsNone()) {
            break;
        }
    }

    if (!extractErr.IsNone()) {
        return extractErr;
    }

    {
        LockGuard lock {mMutex};

        mSystemInfo.mSystemID = systemID;
    }

    LOG_INF() << "System ID updated from online certificate" << Log::Field("systemID", systemID);

    return ErrorEnum::eNone;
}

} // namespace aos::iam::identhandler
