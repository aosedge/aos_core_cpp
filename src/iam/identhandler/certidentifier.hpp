/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_IDENTHANDLER_CERTIDENTIFIER_HPP_
#define AOS_IAM_IDENTHANDLER_CERTIDENTIFIER_HPP_

#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/tools/memory.hpp>
#include <core/common/tools/thread.hpp>
#include <core/iam/identhandler/itf/identmodule.hpp>

#include "certidentifierconfig.hpp"

namespace aos::iam::identhandler {

/**
 * Cert identifier module.
 *
 * Unit model and subjects are read from files.
 * System ID is taken from the online certificate SAN URI (urn:aos:unit:<system-id>).
 */
class CertIdentifier : public IdentModuleItf, public iamclient::CertListenerItf {
public:
    /**
     * Initializes cert identifier.
     *
     * @param config module config.
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param allocator allocator.
     * @return Error.
     */
    Error Init(const CertIdentifierConfig& config, iamclient::CertProviderItf& certProvider,
        crypto::CertLoaderItf& certLoader, AllocatorItf& allocator);

    /**
     * Starts cert identifier.
     *
     * @return Error.
     */
    Error Start() override;

    /**
     * Stops cert identifier.
     *
     * @return Error.
     */
    Error Stop() override;

    /**
     * Returns System info.
     *
     * @param[out] info result system info.
     * @returns Error.
     */
    Error GetSystemInfo(SystemInfo& info) override;

    /**
     * Returns subjects.
     *
     * @param[out] subjects result subjects.
     * @returns Error.
     */
    Error GetSubjects(Array<StaticString<cIDLen>>& subjects) override;

    /**
     * Handles online certificate changes.
     *
     * @param info certificate info.
     */
    void OnCertChanged(const CertInfo& info) override;

private:
    static constexpr auto cModelVersionDelimiter = ';';
    static constexpr auto cOnlineCert            = "online";
    static constexpr auto cWhiteSpaces           = "\n\t ";

    Error ReadUnitModel();
    Error ReadSubjects();
    Error UpdateSystemIDFromCert();

    Mutex                                              mMutex;
    CertIdentifierConfig                               mConfig;
    SystemInfo                                         mSystemInfo;
    StaticArray<StaticString<cIDLen>, cMaxNumSubjects> mSubjects;
    iamclient::CertProviderItf*                        mCertProvider {};
    crypto::CertLoaderItf*                             mCertLoader {};
    AllocatorItf*                                      mAllocator {};
};

} // namespace aos::iam::identhandler

#endif
