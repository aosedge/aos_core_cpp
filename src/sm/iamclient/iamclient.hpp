/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_IAMCLIENT_HPP_
#define AOS_COMMON_IAMCLIENT_IAMCLIENT_HPP_

#include <string>

#include <common/iamclient/itf/permservice.hpp>
#include <common/iamclient/itf/publicservice.hpp>

#include "itf/iamclient.hpp"

namespace aos::sm::iamclient {

/**
 * Configuration.
 */
struct Config {
    std::string mIAMPublicServerURL;
    std::string mCACert;
};

class IAMClient : public IAMClientItf {
public:
    Error Init(
        common::iamclient::PublicServiceItf& publicService, common::iamclient::PermissionsServiceItf& permService);

    /**
     * Returns certificate info.
     *
     * @param certType certificate type.
     * @param issuer issuer name.
     * @param serial serial number.
     * @param[out] resCert result certificate.
     * @returns Error.
     */
    Error GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
        iam::certhandler::CertInfo& resCert) const override;

    /**
     * Subscribes certificates receiver.
     *
     * @param certType certificate type.
     * @param certReceiver certificate receiver.
     * @returns Error.
     */
    Error SubscribeCertChanged(const String& certType, iam::certhandler::CertReceiverItf& certReceiver) override;

    /**
     * Unsubscribes certificate receiver.
     *
     * @param certReceiver certificate receiver.
     * @returns Error.
     */
    Error UnsubscribeCertChanged(iam::certhandler::CertReceiverItf& certReceiver) override;

    /**
     * Adds new service instance and its permissions into cache.
     *
     * @param instanceIdent instance identification.
     * @param instancePermissions instance permissions.
     * @returns RetWithError<StaticString<cSecretLen>>.
     */
    RetWithError<StaticString<iam::permhandler::cSecretLen>> RegisterInstance(
        const InstanceIdent& instanceIdent, const Array<FunctionServicePermissions>& instancePermissions) override;

    /**
     * Unregisters instance deletes service instance with permissions from cache.
     *
     * @param instanceIdent instance identification.
     * @returns Error.
     */
    Error UnregisterInstance(const InstanceIdent& instanceIdent) override;

    /**
     * Returns instance ident and permissions by secret and functional server ID.
     *
     * @param secret secret.
     * @param funcServerID functional server ID.
     * @param[out] instanceIdent result instance ident.
     * @param[out] servicePermissions result service permission.
     * @returns Error.
     */
    Error GetPermissions(const String& secret, const String& funcServerID, InstanceIdent& instanceIdent,
        Array<FunctionPermissions>& servicePermissions) override;

    /**
     * Gets the node info object.
     *
     * @param[out] nodeInfo node info
     * @return Error
     */
    Error GetNodeInfo(NodeInfoObsolete& nodeInfo) const override;

    /**
     * Sets the node state.
     *
     * @param state node state.
     * @return Error.
     */
    Error SetNodeState(const NodeStateObsolete& state) override;

    /**
     * Subscribes on node state changed event.
     *
     * @param observer node state changed observer
     * @return Error
     */
    Error SubscribeNodeStateChanged(iam::nodeinfoprovider::NodeStateObserverItf& observer) override;

    /**
     * Unsubscribes from node state changed event.
     *
     * @param observer node state changed observer
     * @return Error
     */
    Error UnsubscribeNodeStateChanged(iam::nodeinfoprovider::NodeStateObserverItf& observer) override;

private:
    common::iamclient::PublicServiceItf*      mPublicService {};
    common::iamclient::PermissionsServiceItf* mPermService {};
};

} // namespace aos::sm::iamclient

#endif // AOS_COMMON_IAMCLIENT_IAMCLIENT_HPP_