/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>

#include "storagestub.hpp"

namespace aos {
namespace iam {
namespace certhandler {

Error StorageStub::AddCertInfo(const String& certType, const CertInfo& certInfo)
{
    Error err;
    auto  cell = FindCell(certType);

    if (cell == mStorage.end()) {
        err = mStorage.EmplaceBack();
        if (!err.IsNone()) {
            return err;
        }

        cell            = &mStorage.Back();
        cell->mCertType = certType;
    }

    if (std::any_of(cell->mCertificates.begin(), cell->mCertificates.end(),
            [&](const auto& cert) { return cert == certInfo; })) {
        return ErrorEnum::eAlreadyExist;
    }

    return cell->mCertificates.PushBack(certInfo);
}

Error StorageStub::GetCertInfo(const Array<uint8_t>& issuer, const Array<uint8_t>& serial, CertInfo& cert)
{
    for (auto& cell : mStorage) {
        auto it = std::find_if(cell.mCertificates.begin(), cell.mCertificates.end(),
            [&](const auto& cur) { return cur.mIssuer == issuer && cur.mSerial == serial; });

        if (it != cell.mCertificates.end()) {
            cert = *it;
            return ErrorEnum::eNone;
        }
    }

    return ErrorEnum::eNotFound;
}

Error StorageStub::GetCertsInfo(const String& certType, Array<CertInfo>& certsInfo)
{
    const auto* cell = FindCell(certType);
    if (cell == mStorage.end()) {
        return ErrorEnum::eNotFound;
    }

    certsInfo.Clear();

    for (const auto& cert : cell->mCertificates) {
        Error err = certsInfo.PushBack(cert);
        if (!err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error StorageStub::RemoveCertInfo(const String& certType, const String& certURL)
{
    auto* cell = FindCell(certType);
    if (cell == mStorage.end()) {
        return ErrorEnum::eNotFound;
    }

    auto it = std::find_if(cell->mCertificates.begin(), cell->mCertificates.end(),
        [&certURL](const auto& cur) { return cur.mCertURL == certURL; });

    if (it != cell->mCertificates.end()) {
        cell->mCertificates.Remove(*it);

        return ErrorEnum::eNone;
    }

    return ErrorEnum::eNotFound;
}

Error StorageStub::RemoveAllCertsInfo(const String& certType)
{
    auto* cell = FindCell(certType);
    if (cell == mStorage.end()) {
        return ErrorEnum::eNotFound;
    }

    mStorage.Remove(*cell);

    return ErrorEnum::eNone;
}

StorageStub::StorageCell* StorageStub::FindCell(const String& certType)
{
    auto it
        = std::find_if(mStorage.begin(), mStorage.end(), [&](const auto& cell) { return cell.mCertType == certType; });

    if (it != mStorage.end()) {
        return &*it;
    }

    return mStorage.end();
}

} // namespace certhandler
} // namespace iam
} // namespace aos
