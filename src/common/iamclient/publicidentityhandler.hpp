/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PUBLICIDENTITYHANDLER_HPP_
#define AOS_COMMON_IAMCLIENT_PUBLICIDENTITYHANDLER_HPP_

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <core/common/crypto/crypto.hpp>
#include <core/common/crypto/cryptoutils.hpp>
#include <core/common/identprovider/itf/identprovider.hpp>
#include <core/iam/certhandler/certprovider.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include <common/iamclient/publicservicehandler.hpp>
#include <common/utils/grpchelper.hpp>

namespace aos::common::iamclient {

/**
 * Public identity service handler.
 */
class PublicIdentityServiceHandler : public identprovider::IdentProviderItf {
public:
    /**
     * Initializes public identity service handler.
     *
     * @param serviceURL IAM service URL.
     * @param certStorage certificate storage.
     * @param mTLSCredentials TLS credentials.
     * @return Error.
     */
    Error Init(const std::string& serviceURL, const std::string& certStorage, TLSCredentialsItf& TLSCredentials);

    /**
     * Starts ident provider.
     *
     * @returns Error.
     */
    Error Start();

    /**
     * Stops ident provider.
     *
     * @returns Error.
     */
    Error Stop();

    /**
     * Returns System ID.
     *
     * @returns RetWithError<StaticString>.
     */
    RetWithError<StaticString<cIDLen>> GetSystemID() override;

    /**
     * Returns unit model.
     *
     * @returns RetWithError<StaticString>.
     */
    RetWithError<StaticString<cUnitModelLen>> GetUnitModel() override;

    /**
     * Returns subjects.
     *
     * @param[out] subjects result subjects.
     * @returns Error.
     */
    Error GetSubjects(Array<StaticString<cIDLen>>& subjects) override;

    /**
     * Subscribes to subjects changed events.
     *
     * @param observer subjects observer.
     * @returns Error.
     */
    Error SubscribeSubjectsChanged(identprovider::SubjectsObserverItf& observer) override;

    /**
     * Unsubscribes from subjects changed events.
     *
     * @param observer subjects observer.
     */
    void UnsubscribeSubjectsChanged(identprovider::SubjectsObserverItf& observer) override;

private:
    static constexpr auto cRPCCallTimeout = std::chrono::seconds(10);

    void                                      Run();
    std::shared_ptr<grpc::ChannelCredentials> CreateCredentials();
    void                                      ReceiveChangedSubjects();
    void                                      NotifyObservers(const Array<StaticString<cIDLen>>& subjects);

    TLSCredentialsItf*                               mTLSCredentials {};
    std::string                                      mServiceURL;
    std::string                                      mCertStorage;
    std::unique_ptr<grpc::ClientContext>             mSubjectsChangedContext;
    std::vector<identprovider::SubjectsObserverItf*> mObservers;

    std::thread mThread;
    std::mutex  mMutex;
    bool        mIsRunning {false};
};

} // namespace aos::common::iamclient

#endif
