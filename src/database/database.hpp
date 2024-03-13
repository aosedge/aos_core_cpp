/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DATABASE_HPP_
#define DATABASE_HPP_

#include <memory>
#include <optional>
#include <string>

#include <Poco/Data/Session.h>

#include <aos/iam/certmodules/certmodule.hpp>

class Database : public aos::iam::certhandler::StorageItf {
public:
    /**
     * Initializes certificate info storage.
     *
     * @param dbPath path to the database file.
     * @return Error.
     */
    aos::Error Init(const std::string& dbPath);

    /**
     * Adds new certificate info to the storage.
     *
     * @param certType certificate type.
     * @param certInfo certificate information.
     * @return Error.
     */
    aos::Error AddCertInfo(const aos::String& certType, const aos::iam::certhandler::CertInfo& certInfo) override;

    /**
     * Returns information about certificate with specified issuer and serial number.
     *
     * @param issuer certificate issuer.
     * @param serial serial number.
     * @param cert result certificate.
     * @return Error.
     */
    aos::Error GetCertInfo(const aos::Array<uint8_t>& issuer, const aos::Array<uint8_t>& serial,
        aos::iam::certhandler::CertInfo& cert) override;

    /**
     * Returns info for all certificates with specified certificate type.
     *
     * @param certType certificate type.
     * @param certsInfo result certificates info.
     * @return Error.
     */
    aos::Error GetCertsInfo(
        const aos::String& certType, aos::Array<aos::iam::certhandler::CertInfo>& certsInfo) override;

    /**
     * Removes certificate with specified certificate type and url.
     *
     * @param certType certificate type.
     * @param certURL certificate URL.
     * @return Error.
     */
    aos::Error RemoveCertInfo(const aos::String& certType, const aos::String& certURL) override;

    /**
     * Removes all certificates with specified certificate type.
     *
     * @param certType certificate type.
     * @return Error.
     */
    aos::Error RemoveAllCertsInfo(const aos::String& certType) override;

    /**
     * Destroys certificate info storage.
     */
    ~Database();

private:
    enum CertColumns { eType = 0, eIssuer, eSerial, eCertURL, eKeyURL, eNotAfter };
    using CertInfo = Poco::Tuple<std::string, Poco::Data::BLOB, Poco::Data::BLOB, std::string, std::string, uint64_t>;

    void     CreateTables();
    CertInfo ToAosCertInfo(const aos::String& certType, const aos::iam::certhandler::CertInfo& certInfo);
    void     FromAosCertInfo(const CertInfo& certInfo, aos::iam::certhandler::CertInfo& result);

    std::optional<Poco::Data::Session> mSession;
};

#endif
