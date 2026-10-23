/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>

#include <core/common/tools/logger.hpp>

#include "rootcerts.hpp"

namespace aos::cm::rootcerts {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error RootCerts::Init(aos::iamclient::CertHandlerItf& certHandler,
    nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider, cloudconnection::CloudConnectionItf& cloudConnection,
    updatemanager::SenderItf& sender)
{
    LOG_DBG() << "Init root certificates";

    mCertHandler      = &certHandler;
    mNodeInfoProvider = &nodeInfoProvider;
    mCloudConnection  = &cloudConnection;
    mSender           = &sender;

    return ErrorEnum::eNone;
}

Error RootCerts::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start root certificates";

    if (auto err = mCloudConnection->SubscribeListener(*this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mNodeInfoProvider->SubscribeListener(*this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootCerts::Stop()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Stop root certificates";

    if (auto err = mNodeInfoProvider->UnsubscribeListener(*this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mCloudConnection->UnsubscribeListener(*this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootCerts::ProcessDesiredRootCertificates(const DesiredUnitRootCertificates& desiredRootCerts)
{
    LOG_DBG() << "Process desired unit root certificates"
              << Log::Field("count", desiredRootCerts.mNodeCertificates.Size());

    for (const auto& nodeRootCerts : desiredRootCerts.mNodeCertificates) {
        LOG_DBG() << "Update root certificates" << Log::Field("nodeID", nodeRootCerts.mNodeID)
                  << Log::Field("count", nodeRootCerts.mCertificates.Size());

        if (auto err = mCertHandler->UpdateRootCerts(nodeRootCerts.mNodeID, nodeRootCerts.mCertificates);
            !err.IsNone()) {
            LOG_ERR() << "Update root certificates failed" << Log::Field("nodeID", nodeRootCerts.mNodeID)
                      << Log::Field(err);
        }
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void RootCerts::OnNodeInfoChanged(const UnitNodeInfo& info)
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Node info changed" << Log::Field("nodeID", info.mNodeID);

        if (!mCloudConnected) {
            return;
        }
    }

    if (auto err = SendRootCertUpdate(info.mNodeID); !err.IsNone()) {
        LOG_ERR() << "Send node root certificates failed" << Log::Field("nodeID", info.mNodeID) << Log::Field(err);
    }
}

void RootCerts::OnConnect()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Cloud connected";

        mCloudConnected = true;
    }

    if (auto err = SendFullSnapshot(); !err.IsNone()) {
        LOG_ERR() << "Send unit root certificates failed" << Log::Field(err);
    }
}

void RootCerts::OnDisconnect()
{
    std::lock_guard lock {mMutex};

    mCloudConnected = false;
}

Error RootCerts::FillNodeRootCertificates(const String& nodeID, NodeRootCertificates& nodeCerts)
{
    if (auto err = nodeCerts.mNodeID.Assign(nodeID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mCertHandler->GetRootCerts(nodeID, nodeCerts.mSHA256Fingerprints); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootCerts::SendFullSnapshot()
{
    StaticArray<StaticString<cIDLen>, cMaxNumNodes> nodeIDs;

    if (auto err = mNodeInfoProvider->GetAllNodeIDs(nodeIDs); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto unitRootCerts = std::make_unique<UnitRootCertificates>();

    unitRootCerts->mIsPartial = false;

    for (const auto& nodeID : nodeIDs) {
        if (auto err = unitRootCerts->mNodeCertificates.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = FillNodeRootCertificates(nodeID, unitRootCerts->mNodeCertificates.Back()); !err.IsNone()) {
            LOG_ERR() << "Get root certificates failed" << Log::Field("nodeID", nodeID) << Log::Field(err);

            (void)unitRootCerts->mNodeCertificates.PopBack();
            continue;
        }
    }

    LOG_DBG() << "Send unit root certificates" << Log::Field("count", unitRootCerts->mNodeCertificates.Size())
              << Log::Field("isPartial", unitRootCerts->mIsPartial);

    if (auto err = mSender->SendUnitRootCertificates(*unitRootCerts); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootCerts::SendRootCertUpdate(const String& nodeID)
{
    {
        std::lock_guard lock {mMutex};

        if (!mCloudConnected) {
            return ErrorEnum::eNone;
        }
    }

    auto unitRootCerts = std::make_unique<UnitRootCertificates>();

    unitRootCerts->mIsPartial = true;

    if (auto err = unitRootCerts->mNodeCertificates.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = FillNodeRootCertificates(nodeID, unitRootCerts->mNodeCertificates.Back()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Send unit root certificates" << Log::Field("nodeID", nodeID)
              << Log::Field("count", unitRootCerts->mNodeCertificates.Size())
              << Log::Field("isPartial", unitRootCerts->mIsPartial);

    if (auto err = mSender->SendUnitRootCertificates(*unitRootCerts); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::rootcerts
