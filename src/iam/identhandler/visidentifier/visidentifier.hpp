/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_VISIDENTIFIER_VISIDENTIFIER_HPP_
#define AOS_IAM_VISIDENTIFIER_VISIDENTIFIER_HPP_

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <Poco/Dynamic/Var.h>
#include <Poco/Event.h>

#include <core/iam/identhandler/identmodule.hpp>

#include <iam/config/config.hpp>
#include <iam/identhandler/visidentifier/wsclient.hpp>

namespace aos::iam::visidentifier {

/**
 * VIS Subscriptions.
 */
class VISSubscriptions {
public:
    using Handler = std::function<Error(const Poco::Dynamic::Var)>;

    /**
     * Register subscription.
     *
     * @param subscriptionId subscription id.
     * @param subscriptionHandler subscription handler.
     * @return Error.
     */
    void RegisterSubscription(const std::string& subscriptionId, Handler&& subscriptionHandler);

    /**
     * Process subscription.
     *
     * @param subscriptionId subscription id.
     * @param value subscription value.
     * @return Error.
     */
    Error ProcessSubscription(const std::string& subscriptionId, const Poco::Dynamic::Var value);

private:
    std::mutex                     mMutex;
    std::map<std::string, Handler> mSubscriptionMap;
};

/**
 * VIS Identifier.
 */
class VISIdentifier : public identhandler::IdentModuleItf {
public:
    /**
     * Creates a new object instance.
     */
    VISIdentifier();

    /**
     * Initializes vis identifier.
     *
     * @param config identifier config.
     * @param subjectsObserver subject observer.
     * @param uuidProvider UUID provider.
     * @return Error.
     */
    Error Init(const config::IdentifierConfig& config, crypto::UUIDItf& uuidProvider);

    /**
     * Starts vis identifier.
     *
     * @return Error.
     */
    Error Start() override;

    /**
     * Stops vis identifier.
     *
     * @return Error.
     */
    Error Stop() override;

    /**
     * Returns System info.
     *
     * @param[out] info result system info.
     * @returns Error.
     */
    Error GetSystemInfo(SystemInfo& info) override;

    /**
     * Returns subjects.
     *
     * @param[out] subjects result subjects.
     * @returns Error.
     */
    Error GetSubjects(Array<StaticString<cIDLen>>& subjects) override;

    /**
     * Subscribes subjects listener.
     *
     * @param subjectsListener subjects listener.
     * @returns Error.
     */
    Error SubscribeListener(iamclient::SubjectsListenerItf& subjectsListener) override
    {
        if (mSubjectsListener != nullptr) {
            return ErrorEnum::eAlreadyExist;
        }

        mSubjectsListener = &subjectsListener;

        return ErrorEnum::eNone;
    }

    /**
     * Unsubscribes subjects listener.
     *
     * @param subjectsListener subjects listener.
     * @returns Error.
     */
    Error UnsubscribeListener(iamclient::SubjectsListenerItf& subjectsListener) override
    {
        if (mSubjectsListener != &subjectsListener) {
            return ErrorEnum::eNotFound;
        }

        mSubjectsListener = nullptr;

        return ErrorEnum::eNone;
    }

protected:
    virtual Error  InitWSClient(const config::IdentifierConfig& config);
    void           SetWSClient(WSClientItfPtr wsClient);
    WSClientItfPtr GetWSClient();
    void           HandleSubscription(const std::string& message);
    void           WaitUntilConnected();

private:
    static constexpr const char* cVinVISPath                    = "Attribute.Vehicle.VehicleIdentification.VIN";
    static constexpr const char* cUnitModelPath                 = "Attribute.Aos.UnitModel";
    static constexpr const char* cSubjectsVISPath               = "Attribute.Aos.Subjects";
    static const long            cWSClientReconnectMilliseconds = 2000;

    void                     Close();
    void                     HandleConnection();
    Error                    HandleSubjectsSubscription(Poco::Dynamic::Var value);
    std::string              SendGetRequest(const std::string& path);
    void                     SendUnsubscribeAllRequest();
    void                     Subscribe(const std::string& path, VISSubscriptions::Handler&& callback);
    std::string              GetValueByPath(Poco::Dynamic::Var object, const std::string& valueChildTagName);
    std::vector<std::string> GetValueArrayByPath(Poco::Dynamic::Var object, const std::string& valueChildTagName);
    void                     SetSystemID(SystemInfo& info);
    void                     SetUnitModelAndVersion(SystemInfo& info);

    std::shared_ptr<WSClientItf>                       mWsClientPtr;
    iamclient::SubjectsListenerItf*                    mSubjectsListener = nullptr;
    crypto::UUIDItf*                                   mUUIDProvider     = nullptr;
    VISSubscriptions                                   mSubscriptions;
    std::optional<SystemInfo>                          mSystemInfo;
    StaticArray<StaticString<cIDLen>, cMaxNumSubjects> mSubjects;
    std::thread                                        mHandleConnectionThread;
    Poco::Event                                        mWSClientIsConnected;
    Poco::Event                                        mStopHandleSubjectsChangedThread;
    std::mutex                                         mMutex;
    config::IdentifierConfig                           mConfig;
};

} // namespace aos::iam::visidentifier

#endif
