/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMPUBLICSERVICE_HPP
#define IAMPUBLICSERVICE_HPP

#include <grpcpp/security/credentials.h>
#include <grpcpp/server_builder.h>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include <aos/iam/certhandler.hpp>

/**
 * Test IAM server.
 */
class TestIAMServer final : public iamanager::v5::IAMPublicService::Service {
public:
    /**
     * Constructor.
     */
    TestIAMServer() { mServer = CreateServer(); }

    /**
     * Get certificate type.
     *
     * @return Certificate type.
     */
    std::string GetCertType() const { return mCertType; }

    /**
     * Set certificate info.
     *
     * @param certInfo Certificate info.
     */
    void SetCertInfo(const aos::iam::certhandler::CertInfo& certInfo) { mCertInfo = certInfo; }

    /**
     * Send certificate changed info.
     *
     * @param certInfo Certificate info.
     * @return True if success.
     */
    bool SendCertChangedInfo(const iamanager::v5::CertInfo& certInfo)
    {
        mCertInfo.mCertURL = certInfo.cert_url().c_str();
        mCertInfo.mKeyURL  = certInfo.key_url().c_str();

        return mStream->Write(certInfo);
    }

    /**
     * Wait for connection.
     *
     * @return True if connected.
     */
    bool WaitForConnection()
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, kTimeout, [this] { return mConnected; });

        return mConnected;
    }

    /**
     * Close server.
     */
    void Close()
    {
        {
            std::lock_guard lock {mLock};
            mClose = true;
        }

        mCV.notify_all();
    }

private:
    constexpr static std::chrono::seconds kTimeout = std::chrono::seconds(5);

    std::unique_ptr<grpc::Server> CreateServer()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8002", grpc::InsecureServerCredentials());
        builder.RegisterService(static_cast<iamanager::v5::IAMPublicService::Service*>(this));

        return builder.BuildAndStart();
    }

    grpc::Status GetCert(
        grpc::ServerContext*, const iamanager::v5::GetCertRequest* request, iamanager::v5::CertInfo* response) override
    {

        mCertType = request->type();

        response->set_cert_url(mCertInfo.mCertURL.CStr());
        response->set_key_url(mCertInfo.mKeyURL.CStr());

        return grpc::Status::OK;
    }

    grpc::Status SubscribeCertChanged(grpc::ServerContext*, const iamanager::v5::SubscribeCertChangedRequest* request,
        grpc::ServerWriter<iamanager::v5::CertInfo>* writer)
    {
        mCertType = request->type();

        mStream = writer;

        mConnected = true;
        mCV.notify_all();

        {
            std::unique_lock lock {mLock};
            mCV.wait(lock, [this] { return mClose; });
        }

        return grpc::Status::OK;
    }

    std::unique_ptr<grpc::Server>                mServer;
    std::string                                  mCertType;
    aos::iam::certhandler::CertInfo              mCertInfo;
    grpc::ServerWriter<iamanager::v5::CertInfo>* mStream {};
    std::mutex                                   mLock;
    std::condition_variable                      mCV;
    bool                                         mConnected = false;
    bool                                         mClose     = false;
};

#endif // IAMPUBLICSERVICE_HPP
