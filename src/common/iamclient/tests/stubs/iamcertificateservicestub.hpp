/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMCERTIFICATESERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMCERTIFICATESERVICESTUB_HPP_

#include <memory>
#include <mutex>
#include <string>

#include <grpcpp/grpcpp.h>
#include <iamanager/v6/iamanager.grpc.pb.h>
#include <iamanager/v6/iamanager.pb.h>

/**
 * Test stub for IAMCertificateService v6.
 */
class IAMCertificateServiceStub final : public iamanager::v6::IAMCertificateService::Service {
public:
    IAMCertificateServiceStub()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8009", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        mServer = builder.BuildAndStart();
    }

    ~IAMCertificateServiceStub()
    {
        if (mServer) {
            mServer->Shutdown();
        }
    }

    void SetCSR(const std::string& csr)
    {
        std::lock_guard lock {mMutex};

        mCSR = csr;
    }

    void SetCertInfo(const std::string& certURL, const std::string& keyURL)
    {
        std::lock_guard lock {mMutex};

        mCertURL = certURL;
        mKeyURL  = keyURL;
    }

    void SetError(int exitCode, const std::string& message)
    {
        std::lock_guard lock {mMutex};

        mHasError      = true;
        mErrorExitCode = exitCode;
        mErrorMessage  = message;
    }

    void ClearError()
    {
        std::lock_guard lock {mMutex};

        mHasError = false;
    }

    std::string GetLastNodeID() const
    {
        std::lock_guard lock {mMutex};

        return mLastNodeID;
    }

    std::string GetLastCertType() const
    {
        std::lock_guard lock {mMutex};

        return mLastCertType;
    }

    std::string GetLastSubject() const
    {
        std::lock_guard lock {mMutex};

        return mLastSubject;
    }

    std::string GetLastPassword() const
    {
        std::lock_guard lock {mMutex};

        return mLastPassword;
    }

    std::string GetLastPemCert() const
    {
        std::lock_guard lock {mMutex};

        return mLastPemCert;
    }

    grpc::Status CreateKey([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::CreateKeyRequest* request, iamanager::v6::CreateKeyResponse* response) override
    {
        std::lock_guard lock {mMutex};

        mLastNodeID   = request->node_id();
        mLastCertType = request->type();
        mLastSubject  = request->subject();
        mLastPassword = request->password();

        if (mHasError) {
            response->mutable_error()->set_exit_code(mErrorExitCode);
            response->mutable_error()->set_message(mErrorMessage);
        } else {
            response->set_csr(mCSR);
        }

        return grpc::Status::OK;
    }

    grpc::Status ApplyCert([[maybe_unused]] grpc::ServerContext* context,
        const iamanager::v6::ApplyCertRequest* request, iamanager::v6::ApplyCertResponse* response) override
    {
        std::lock_guard lock {mMutex};

        mLastNodeID   = request->node_id();
        mLastCertType = request->type();
        mLastPemCert  = request->cert();

        if (mHasError) {
            response->mutable_error()->set_exit_code(mErrorExitCode);
            response->mutable_error()->set_message(mErrorMessage);
        } else {
            response->mutable_cert_info()->set_cert_url(mCertURL);
            response->mutable_cert_info()->set_key_url(mKeyURL);
        }

        return grpc::Status::OK;
    }

private:
    std::unique_ptr<grpc::Server> mServer;
    mutable std::mutex            mMutex;
    std::string                   mCSR;
    std::string                   mCertURL;
    std::string                   mKeyURL;
    bool                          mHasError {false};
    int                           mErrorExitCode {0};
    std::string                   mErrorMessage;
    std::string                   mLastNodeID;
    std::string                   mLastCertType;
    std::string                   mLastSubject;
    std::string                   mLastPassword;
    std::string                   mLastPemCert;
};

#endif
