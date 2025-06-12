/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_STORAGE_STUB_HPP_
#define AOS_STORAGE_STUB_HPP_

#include "aos/iam/certmodules/certmodule.hpp"

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
     * @param certType certificate type.
     * @param certInfo certificate information.
     * @return Error.
     */
    Error AddCertInfo(const String& certType, const CertInfo& certInfo) override;

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
     * @param certType certificate type.
     * @param[out] certsInfo result certificates info.
     * @return Error.
     */
    Error GetCertsInfo(const String& certType, Array<CertInfo>& certsInfo) override;

    /**
     * Removes certificate with specified certificate type and url.
     *
     * @param certType certificate type.
     * @param certURL certificate URL.
     * @return Error.
     */
    Error RemoveCertInfo(const String& certType, const String& certURL) override;

    /**
     * Removes all certificates with specified certificate type.
     *
     * @param certType certificate type.
     * @return Error.
     */
    Error RemoveAllCertsInfo(const String& certType) override;

    /**
     * Destroys certificate info storage.
     */
    ~StorageStub() override = default;

private:
    static constexpr auto cCellSize      = 20;
    static constexpr auto cCertTypeCount = 4;

    struct StorageCell {
        StaticString<cCertTypeLen>       mCertType;
        StaticArray<CertInfo, cCellSize> mCertificates;
    };

    template <typename Res, typename Func>
    Res TraverseCertTypes(Func func);

    StorageCell* FindCell(const String& certType);

    StaticArray<StorageCell, cCertTypeCount> mStorage;
};

} // namespace certhandler
} // namespace iam
} // namespace aos

#endif
