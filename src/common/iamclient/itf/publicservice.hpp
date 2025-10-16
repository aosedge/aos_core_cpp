/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_ITF_PUBLICSERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_ITF_PUBLICSERVICE_HPP_

#include <core/common/types/obsolete.hpp>
#include <core/iam/certhandler/certhandler.hpp>
#include <core/iam/certhandler/hsm.hpp>

namespace aos::common::iamclient {

/**
 * Public service interface.
 */
class PublicServiceItf {
public:
    /**
     * Destructor.
     */
    virtual ~PublicServiceItf() = default;

    /**
     * Returns certificate info.
     *
     * @param certType certificate type.
     * @param issuer issuer name.
     * @param serial serial number.
     * @param[out] resCert result certificate.
     * @returns Error.
     */
    virtual Error GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
        iam::certhandler::CertInfo& resCert) const
        = 0;

    /**
     * Subscribes certificates receiver.
     *
     * @param certType certificate type.
     * @param certReceiver certificate receiver.
     * @returns Error.
     */
    virtual Error SubscribeCertChanged(const String& certType, iam::certhandler::CertReceiverItf& certReceiver) = 0;

    /**
     * Unsubscribes certificate receiver.
     *
     * @param certReceiver certificate receiver.
     * @returns Error.
     */
    virtual Error UnsubscribeCertChanged(iam::certhandler::CertReceiverItf& certReceiver) = 0;

    /**
     * Gets the node info object.
     *
     * @param[out] nodeInfo node info
     * @return Error
     */
    virtual Error GetNodeInfo(NodeInfoObsolete& nodeInfo) const = 0;
};

} // namespace aos::common::iamclient

#endif
