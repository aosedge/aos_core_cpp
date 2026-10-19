/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_STORAGE_STUB_HPP_
#define AOS_STORAGE_STUB_HPP_

#include <core/iam/certhandler/certmodule.hpp>

namespace aos {
namespace iam {
namespace certhandler {
/**
 * Stoage stub.
 */
class StorageStub : public StorageItf {
public:
    /**
     * Adds new certificate info to the storage.
     *
     * @param storageID certificate storage ID.
     * @param certInfo certificate information.
     * @return Error.
     */
    Error AddCertInfo(const String& storageID, const CertInfo& certInfo) override;

    /**
     * Returns information about certificate with specified issuer and serial number.
     *
     * @param issuer certificate issuer.
     * @param serial serial number.
     * @param cert result certificate.
     * @return Error.
     */
    Error GetCertInfo(const Array<uint8_t>& issuer, const Array<uint8_t>& serial, CertInfo& cert) override;

    /**
     * Returns info for all certificates with specified certificate type.
     *
     * @param storageID certificate storage ID.
     * @param[out] certsInfo result certificates info.
     * @return Error.
     */
    Error GetCertsInfo(const String& storageID, Array<CertInfo>& certsInfo) override;

    /**
     * Removes certificate with specified certificate type and url.
     *
     * @param storageID certificate storage ID.
     * @param certURL certificate URL.
     * @return Error.
     */
    Error RemoveCertInfo(const String& storageID, const String& certURL) override;

    /**
     * Removes all certificates with specified certificate type.
     *
     * @param storageID certificate storage ID.
     * @return Error.
     */
    Error RemoveAllCertsInfo(const String& storageID) override;

    /**
     * Destroys certificate info storage.
     */
    ~StorageStub() override = default;

private:
    static constexpr auto cCellSize       = 20;
    static constexpr auto cStorageIDCount = 4;

    struct StorageCell {
        StaticString<cStorageIDLen>      mStorageID;
        StaticArray<CertInfo, cCellSize> mCertificates;
    };

    template <typename Res, typename Func>
    Res TraverseStorageIDs(Func func);

    StorageCell* FindCell(const String& storageID);

    StaticArray<StorageCell, cStorageIDCount> mStorage;
};

} // namespace certhandler
} // namespace iam
} // namespace aos

#endif
