#include "storagestub.hpp"

namespace aos {
namespace iam {
namespace certhandler {

Error StorageStub::AddCertInfo(const String& certType, const CertInfo& certInfo)
{
    Error err  = ErrorEnum::eNone;
    auto  cell = FindCell(certType);

    if (cell == mStorage.end()) {
        err = mStorage.EmplaceBack();
        if (!err.IsNone()) {
            return err;
        }

        cell            = &mStorage.Back().mValue;
        cell->mCertType = certType;
    }

    for (const auto& cert : cell->mCertificates) {
        if (cert == certInfo) {
            return ErrorEnum::eAlreadyExist;
        }
    }

    return cell->mCertificates.PushBack(certInfo);
}

Error StorageStub::GetCertInfo(const Array<uint8_t>& issuer, const Array<uint8_t>& serial, CertInfo& cert)
{
    for (auto& cell : mStorage) {
        for (auto& cur : cell.mCertificates) {
            if (cur.mIssuer == issuer && cur.mSerial == serial) {
                cert = cur;
                return ErrorEnum::eNone;
            }
        }
    }

    return ErrorEnum::eNotFound;
}

Error StorageStub::GetCertsInfo(const String& certType, Array<CertInfo>& certsInfo)
{
    auto* cell = FindCell(certType);
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

    for (auto& cur : cell->mCertificates) {
        if (cur.mCertURL == certURL) {
            cell->mCertificates.Erase(&cur);

            return ErrorEnum::eNone;
        }
    }

    return ErrorEnum::eNotFound;
}

Error StorageStub::RemoveAllCertsInfo(const String& certType)
{
    auto* cell = FindCell(certType);
    if (cell == mStorage.end()) {
        return ErrorEnum::eNotFound;
    }

    mStorage.Erase(cell);

    return ErrorEnum::eNone;
}

StorageStub::StorageCell* StorageStub::FindCell(const String& certType)
{
    for (auto& cell : mStorage) {
        if (cell.mCertType == certType) {
            return &cell;
        }
    }

    return mStorage.end();
}

} // namespace certhandler
} // namespace iam
} // namespace aos
