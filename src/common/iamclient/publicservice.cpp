/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <common/logger/logmodule.hpp>
#include <common/pbconvert/common.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/grpchelper.hpp>

#include "publicservice.hpp"

namespace aos::common::iamclient {
/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

PublicService::~PublicService()
{
    // Explicitly close all subscription managers before mStub destructs
    // This ensures RunTask() doesn't access an invalid stub pointer
    for (auto& [certType, manager] : mSubscriptions) {
        if (manager) {
            manager->Close();
        }
    }
}

Error PublicService::Init(const std::string& iamPublicServerURL, const std::string& CACert,
    TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    LOG_INF() << "Init public service: iamPublicServerURL=" << iamPublicServerURL.c_str()
              << ", insecureConnection=" << insecureConnection;

    mTLSCredentials     = &tlsCredentials;
    mIAMPublicServerURL = iamPublicServerURL;

    if (auto err = CreateCredentials(insecureConnection, CACert); !err.IsNone()) {
        return err;
    }

    mStub = iamanager::v5::IAMPublicService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicService::GetNodeInfo(NodeInfoObsolete& nodeInfo) const
{
    LOG_DBG() << "Get node info";

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cIAMPublicServiceTimeout);

    iamanager::v5::NodeInfo nodeInfoResponse;

    if (auto status = mStub->GetNodeInfo(ctx.get(), google::protobuf::Empty {}, &nodeInfoResponse); !status.ok()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eRuntime, status.error_message().c_str()));
    }

    if (auto err = pbconvert::ConvertToAos(nodeInfoResponse, nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error PublicService::SubscribeCertChanged(const String& certType, iam::certhandler::CertReceiverItf& certReceiver)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Subscribe to certificate changed: certType=" << certType;

    auto& manager = mSubscriptions[certType.CStr()];

    // Create SubscriptionManager if this is the first subscriber for this cert type
    if (!manager) {
        manager = std::make_unique<SubscriptionManager>(certType.CStr(), mStub.get());
    }

    return manager->AddSubscriber(certReceiver);
}

Error PublicService::UnsubscribeCertChanged(iam::certhandler::CertReceiverItf& certReceiver)
{
    std::lock_guard lock {mMutex};

    for (auto it = mSubscriptions.begin(); it != mSubscriptions.end();) {
        auto& manager = it->second;

        // Remove subscriber and check if it was the last one
        if (manager->RemoveSubscriber(certReceiver)) {
            LOG_INF() << "Unsubscribe from certificate changed: certType=" << it->first.c_str();
            // Last subscriber removed, manager will stop task in RemoveSubscriber
            // Remove the manager from the map
            it = mSubscriptions.erase(it);
        } else {
            ++it;
        }
    }

    return ErrorEnum::eNone;
}

Error PublicService::GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
    iam::certhandler::CertInfo& resCert) const
{
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cIAMPublicServiceTimeout);

    iamanager::v5::GetCertRequest request;
    iamanager::v5::CertInfo       certInfoResponse;

    request.set_type(certType.CStr());

    aos::StaticString<aos::crypto::cSerialNumStrLen> serialStr;

    auto err = serialStr.ByteArrayToHex(serial);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    request.set_issuer(issuer.Get(), issuer.Size());
    request.set_serial(serialStr.CStr());

    if (auto status = mStub->GetCert(ctx.get(), request, &certInfoResponse); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    resCert.mCertURL = certInfoResponse.cert_url().c_str();
    resCert.mKeyURL  = certInfoResponse.key_url().c_str();

    LOG_DBG() << "Certificate received: certURL=" << resCert.mCertURL.CStr() << ", keyURL=" << resCert.mKeyURL.CStr();

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error PublicService::CreateCredentials(bool insecureConnection, const std::string& CACert)
{
    try {
        if (insecureConnection) {
            mCredentials = grpc::InsecureChannelCredentials();

            return ErrorEnum::eNone;
        }

        mCredentials = common::utils::GetTLSClientCredentials(CACert.c_str());
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e, ErrorEnum::eRuntime));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::iamclient
