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
#include <vector>

#include <grpcpp/security/credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <google/protobuf/empty.pb.h>
#include <iamanager/v7/iamanager.grpc.pb.h>

#include <common/utils/grpchelper.hpp>

/**
 * Test stub for IAMPublicCertificateService v7.
 */
class IAMPublicCertServiceStub final : public iamanager::v7::IAMPublicCertificateService::Service {
public:
    IAMPublicCertServiceStub() { mServer = CreateServer(); }

    ~IAMPublicCertServiceStub() { Close(); }

    void SetCertInfo(const std::string& certURL, const std::string& keyURL)
    {
        mCertURL = certURL;
        mKeyURL  = keyURL;
    }

    void AddAllCert(const std::string& certURL, const std::string& keyURL) { mAllCerts.push_back({certURL, keyURL}); }

    std::string GetRequestedStorageID() const { return mRequestedStorageID; }

    bool SendCertChanged(const std::string& storageID, const std::string& certURL, const std::string& keyURL)
    {
        std::lock_guard lock {mMutex};

        auto it = mWriters.find(storageID);
        if (it == mWriters.end() || !it->second) {
            return false;
        }

        iamanager::v7::CertKeyInfo certInfo;

        certInfo.set_storage_id(storageID);
        certInfo.set_cert_url(certURL);
        certInfo.set_key_url(keyURL);

        return it->second->Write(certInfo);
    }

    bool WaitForConnection(const std::string& storageID = "", std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};

        if (storageID.empty()) {
            return mCV.wait_for(lock, timeout, [this] { return !mWriters.empty(); });
        }

        return mCV.wait_for(lock, timeout, [this, &storageID] { return mWriters.count(storageID) > 0; });
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

        aos::common::utils::SetGRPCServerOptions(builder);

        builder.AddListeningPort("localhost:8003", grpc::InsecureServerCredentials());
        builder.RegisterService(this);
        return builder.BuildAndStart();
    }

    grpc::Status GetCertKey(grpc::ServerContext*, const iamanager::v7::GetCertKeyRequest* request,
        iamanager::v7::CertKeyInfo* response) override
    {
        mRequestedStorageID = request->storage_id();

        response->set_cert_url(mCertURL);
        response->set_key_url(mKeyURL);
        response->set_storage_id(request->storage_id());

        return grpc::Status::OK;
    }

    grpc::Status GetRootCerts(
        grpc::ServerContext*, const google::protobuf::Empty*, iamanager::v7::RootCertsInfos* response) override
    {
        for (const auto& [certURL, keyURL] : mAllCerts) {
            (void)keyURL;

            response->add_root_certs()->set_cert_url(certURL);
        }

        return grpc::Status::OK;
    }

    grpc::Status SubscribeCertKeyChanged(grpc::ServerContext* context,
        const iamanager::v7::SubscribeCertKeyChangedRequest*  request,
        grpc::ServerWriter<iamanager::v7::CertKeyInfo>*       writer) override
    {
        std::string storageID = request->storage_id();

        {
            std::lock_guard lock {mMutex};

            mWriters[storageID] = writer;
        }

        mCV.notify_all();

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        {
            std::lock_guard lock {mMutex};

            auto it = mWriters.find(storageID);
            if (it != mWriters.end() && it->second == writer) {
                mWriters.erase(it);
            }
        }

        return grpc::Status::OK;
    }

    grpc::Status SubscribeRootCertsChanged(grpc::ServerContext*, const google::protobuf::Empty*,
        grpc::ServerWriter<iamanager::v7::RootCertsInfos>*) override
    {
        return grpc::Status::OK;
    }

    std::unique_ptr<grpc::Server>                                          mServer;
    std::string                                                            mCertURL;
    std::string                                                            mKeyURL;
    std::string                                                            mRequestedStorageID;
    std::vector<std::pair<std::string, std::string>>                       mAllCerts;
    std::map<std::string, grpc::ServerWriter<iamanager::v7::CertKeyInfo>*> mWriters;
    std::mutex                                                             mMutex;
    std::condition_variable                                                mCV;
    bool                                                                   mClose {false};
};

#endif
