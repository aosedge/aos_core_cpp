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
class TestIAMServer final : public iamanager::v5::IAMPublicService::Service,
                            public iamanager::v5::IAMPublicNodesService::Service {
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
     * Send incoming message.
     *
     * @param message Message.
     * @return True if success.
     */
    bool SendIncomingMessage(const iamanager::v5::IAMIncomingMessages& message) { return mStream->Write(message); }

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
     * Wait for response.
     *
     * @param timeout Timeout.
     */
    void WaitResponse(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mCV.wait_for(lock, timeout);
    }

    /**
     * Get outgoing message.
     *
     * @return Outgoing message.
     */
    iamanager::v5::IAMOutgoingMessages GetOutgoingMessage() const { return mOutgoingMsg; }

private:
    constexpr static std::chrono::seconds kTimeout = std::chrono::seconds(5);

    std::unique_ptr<grpc::Server> CreateServer()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8002", grpc::InsecureServerCredentials());
        builder.RegisterService(static_cast<iamanager::v5::IAMPublicService::Service*>(this));
        builder.RegisterService(static_cast<iamanager::v5::IAMPublicNodesService::Service*>(this));

        return builder.BuildAndStart();
    }

    grpc::Status GetCert(grpc::ServerContext*, const iamanager::v5::GetCertRequest* request,
        iamanager::v5::GetCertResponse* response) override
    {

        mCertType = request->type();

        response->set_cert_url(mCertInfo.mCertURL.CStr());
        response->set_key_url(mCertInfo.mKeyURL.CStr());

        return grpc::Status::OK;
    }

    grpc::Status RegisterNode(grpc::ServerContext*,
        grpc::ServerReaderWriter<iamanager::v5::IAMIncomingMessages, iamanager::v5::IAMOutgoingMessages>* stream)
    {
        try {
            mStream = stream;

            iamanager::v5::IAMOutgoingMessages incomingMsg;

            mConnected = true;
            mCV.notify_all();

            while (stream->Read(&incomingMsg)) {
                mOutgoingMsg = incomingMsg;
                mCV.notify_all();
            };
        } catch (const std::exception& e) {
        }

        return grpc::Status::OK;
    }

    std::unique_ptr<grpc::Server>      mServer;
    std::string                        mCertType;
    aos::iam::certhandler::CertInfo    mCertInfo;
    iamanager::v5::IAMOutgoingMessages mOutgoingMsg;

    grpc::ServerReaderWriter<iamanager::v5::IAMIncomingMessages, iamanager::v5::IAMOutgoingMessages>* mStream {};
    std::mutex                                                                                        mLock;
    std::condition_variable                                                                           mCV;
    bool mConnected = false;
};

#endif // IAMPUBLICSERVICE_HPP
