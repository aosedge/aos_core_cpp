/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "iamclient.hpp"

namespace aos::sm::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error IAMClient::Init(
    common::iamclient::PublicServiceItf& publicService, common::iamclient::PermissionsServiceItf& permService)
{
    mPublicService = &publicService;
    mPermService   = &permService;

    return ErrorEnum::eNone;
}

Error IAMClient::GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
    iam::certhandler::CertInfo& resCert) const
{
    return mPublicService->GetCert(certType, issuer, serial, resCert);
}

Error IAMClient::SubscribeCertChanged(const String& certType, iam::certhandler::CertReceiverItf& certReceiver)
{
    return mPublicService->SubscribeCertChanged(certType, certReceiver);
}

Error IAMClient::UnsubscribeCertChanged(iam::certhandler::CertReceiverItf& certReceiver)
{
    return mPublicService->UnsubscribeCertChanged(certReceiver);
}

RetWithError<StaticString<iam::permhandler::cSecretLen>> IAMClient::RegisterInstance(
    const InstanceIdent& instanceIdent, const Array<FunctionServicePermissions>& instancePermissions)
{
    return mPermService->RegisterInstance(instanceIdent, instancePermissions);
}

Error IAMClient::UnregisterInstance(const InstanceIdent& instanceIdent)
{
    return mPermService->UnregisterInstance(instanceIdent);
}

Error IAMClient::GetPermissions(const String& secret, const String& funcServerID, InstanceIdent& instanceIdent,
    Array<FunctionPermissions>& servicePermissions)
{
    return mPermService->GetPermissions(secret, funcServerID, instanceIdent, servicePermissions);
}

Error IAMClient::GetNodeInfo([[maybe_unused]] NodeInfoObsolete& nodeInfo) const
{
    return mPublicService->GetNodeInfo(nodeInfo);
}

Error IAMClient::SetNodeState([[maybe_unused]] const NodeStateObsolete& state)
{
    return ErrorEnum::eNotSupported;
}

Error IAMClient::SubscribeNodeStateChanged([[maybe_unused]] iam::nodeinfoprovider::NodeStateObserverItf& observer)
{
    return ErrorEnum::eNotSupported;
}

Error IAMClient::UnsubscribeNodeStateChanged([[maybe_unused]] iam::nodeinfoprovider::NodeStateObserverItf& observer)
{
    return ErrorEnum::eNotSupported;
}

}; // namespace aos::sm::iamclient
