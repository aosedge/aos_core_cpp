/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REMOTE_IAM_HANDLER_HPP_
#define REMOTE_IAM_HANDLER_HPP_

#include <aos/common/tools/string.hpp>
#include <aos/common/types.hpp>
#include <aos/iam/certhandler.hpp>
#include <string>
#include <vector>

/**
 * Remote IAM's handler.
 */
class RemoteIAMHandlerItf {
public:
    /**
     * Returns remote node identifiers.
     *
     * @result aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, aos::cMaxNumNodes>.
     */
    virtual aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, aos::cMaxNumNodes> GetRemoteNodes() = 0;

    /**
     * Returns IAM cert types.
     *
     * @param nodeID node id.
     * @param[out] certTypes result certificate types.
     * @returns aos::Error.
     */
    virtual aos::Error GetCertTypes(
        const aos::String& nodeID, aos::Array<aos::StaticString<aos::iam::certhandler::cCertTypeLen>>& certTypes)
        = 0;

    /**
     * Owns security storage.
     *
     * @param nodeID node id.
     * @param certType certificate type.
     * @param password owner password.
     * @returns aos::Error.
     */
    virtual aos::Error SetOwner(const aos::String& nodeID, const aos::String& certType, const aos::String& password)
        = 0;

    /**
     * Clears security storage.
     *
     * @param nodeID node id.
     * @param certType certificate type.
     * @returns aos::Error.
     */
    virtual aos::Error Clear(const aos::String& nodeID, const aos::String& certType) = 0;

    /**
     * Creates key pair.
     *
     * @param nodeID node id.
     * @param certType certificate type.
     * @param subjectCommonName common name of the subject.
     * @param password owner password.
     * @param[out] pemCSR certificate signing request in PEM.
     * @returns aos::Error.
     */
    virtual aos::Error CreateKey(const aos::String& nodeID, const aos::String& certType,
        const aos::String& subjectCommonName, const aos::String& password, aos::String& pemCSR)
        = 0;

    /**
     * Applies certificate.
     *
     * @param nodeID node id.
     * @param certType certificate type.
     * @param pemCert certificate in a pem format.
     * @param[out] info result certificate information.
     * @returns aos::Error.
     */
    virtual aos::Error ApplyCertificate(const aos::String& nodeID, const aos::String& certType,
        const aos::String& pemCert, aos::iam::certhandler::CertInfo& info)
        = 0;

    /**
     * Encrypts disk for a node.
     *
     * @param nodeID node identifier.
     * @param password password.
     * @returns aos::Error.
     */
    virtual aos::Error EncryptDisk(const aos::String& nodeID, const aos::String& password) = 0;

    /**
     * Finishes provisioning.
     *
     * @param nodeID node identifier.
     * @returns aos::Error.
     */
    virtual aos::Error FinishProvisioning(const aos::String& nodeID) = 0;

    /**
     * Destroys object instance
     */
    virtual ~RemoteIAMHandlerItf() = default;
};

#endif
