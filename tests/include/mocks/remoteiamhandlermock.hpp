/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REMOTEIAMHANDLER_ITF_MOCK_HPP_
#define REMOTEIAMHANDLER_ITF_MOCK_HPP_

#include <gmock/gmock.h>
#include <iamclient/remoteiamhandler.hpp>

/**
 * RemoteIAMHandler interface mock
 */
class RemoteIAMHandlerMock : public RemoteIAMHandlerItf {
public:
    MOCK_METHOD(
        (aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, aos::cMaxNumNodes>), GetRemoteNodes, (), (override));

    MOCK_METHOD(aos::Error, GetCertTypes,
        (const aos::String& nodeID, aos::Array<aos::StaticString<aos::iam::certhandler::cCertTypeLen>>& certTypes),
        (override));

    MOCK_METHOD(aos::Error, SetOwner,
        (const aos::String& nodeID, const aos::String& certType, const aos::String& password), (override));
    MOCK_METHOD(aos::Error, Clear, (const aos::String& nodeID, const aos::String& certType), (override));
    MOCK_METHOD(aos::Error, CreateKey,
        (const aos::String& nodeID, const aos::String& certType, const aos::String& subjectCommonName,
            const aos::String& password, aos::String& pemCSR),
        (override));

    MOCK_METHOD(aos::Error, ApplyCertificate,
        (const aos::String& nodeID, const aos::String& certType, const aos::String& pemCert,
            aos::iam::certhandler::CertInfo& info),
        (override));

    MOCK_METHOD(aos::Error, EncryptDisk, (const aos::String& nodeID, const aos::String& password), (override));
    MOCK_METHOD(aos::Error, FinishProvisioning, (const aos::String& nodeID), (override));
};

#endif
