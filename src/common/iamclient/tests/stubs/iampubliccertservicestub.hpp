/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICCERTSERVICESTUB_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_STUBS_IAMPUBLICCERTSERVICESTUB_HPP_

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <grpcpp/security/credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <iamanager/v6/iamanager.grpc.pb.h>

/**
 * Test stub for IAMPublicCertService v6.
 */
class IAMPublicCertServiceStub final : public iamanager::v6::IAMPublicCertService::Service {
public:
    IAMPublicCertServiceStub() { mServer = CreateServer(); }

    ~IAMPublicCertServiceStub() { Close(); }

    void SetCertInfo(const std::string& certURL, const std::string& keyURL)
    {
        mCertURL = certURL;
        mKeyURL  = keyURL;
    }

    std::string GetRequestedCertType() const { return mRequestedCertType; }

    bool SendCertChanged(const std::string& certType, const std::string& certURL, const std::string& keyURL)
    {
        std::lock_guard lock {mMutex};

        auto it = mWriters.find(certType);
        if (it == mWriters.end() || !it->second) {
            return false;
        }

        iamanager::v6::CertInfo certInfo;
        certInfo.set_type(certType);
        certInfo.set_cert_url(certURL);
        certInfo.set_key_url(keyURL);

        return it->second->Write(certInfo);
    }

    bool WaitForConnection(const std::string& certType = "", std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};
        if (certType.empty()) {
            return mCV.wait_for(lock, timeout, [this] { return !mWriters.empty(); });
        }
        return mCV.wait_for(lock, timeout, [this, &certType] { return mWriters.count(certType) > 0; });
    }

    void Close()
    {
        {
            std::lock_guard lock {mMutex};
            mClose = true;
        }

        mCV.notify_all();

        if (mServer) {
            mServer->Shutdown();
        }
    }

private:
    std::unique_ptr<grpc::Server> CreateServer()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:8003", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        return builder.BuildAndStart();
    }

    grpc::Status GetCert(
        grpc::ServerContext*, const iamanager::v6::GetCertRequest* request, iamanager::v6::CertInfo* response) override
    {
        mRequestedCertType = request->type();

        response->set_cert_url(mCertURL);
        response->set_key_url(mKeyURL);
        response->set_type(request->type());

        return grpc::Status::OK;
    }

    grpc::Status SubscribeCertChanged(grpc::ServerContext* context,
        const iamanager::v6::SubscribeCertChangedRequest*  request,
        grpc::ServerWriter<iamanager::v6::CertInfo>*       writer) override
    {
        std::string certType = request->type();

        {
            std::lock_guard lock {mMutex};
            mWriters[certType] = writer;
        }

        mCV.notify_all();

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        {
            std::lock_guard lock {mMutex};
            auto            it = mWriters.find(certType);
            if (it != mWriters.end() && it->second == writer) {
                mWriters.erase(it);
            }
        }

        return grpc::Status::OK;
    }

    std::unique_ptr<grpc::Server>                                       mServer;
    std::string                                                         mCertURL;
    std::string                                                         mKeyURL;
    std::string                                                         mRequestedCertType;
    std::map<std::string, grpc::ServerWriter<iamanager::v6::CertInfo>*> mWriters;
    std::mutex                                                          mMutex;
    std::condition_variable                                             mCV;
    bool                                                                mClose {false};
};

#endif
