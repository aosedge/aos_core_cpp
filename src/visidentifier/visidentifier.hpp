/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VISIDENTIFIER_HPP_
#define VISIDENTIFIER_HPP_

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <Poco/Dynamic/Var.h>
#include <Poco/Event.h>

#include <aos/iam/identhandler.hpp>

#include "config/config.hpp"
#include "visidentifier/wsclient.hpp"

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
class VISIdentifier : public iam::identhandler::IdentHandlerItf {
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
     * @return Error.
     */
    Error Init(const config::IdentifierConfig& config, iam::identhandler::SubjectsObserverItf& subjectsObserver);

    /**
     * Returns System ID.
     *
     * @returns RetWithError<StaticString>.
     */
    RetWithError<StaticString<cSystemIDLen>> GetSystemID() override;

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
    Error GetSubjects(Array<StaticString<cSubjectIDLen>>& subjects) override;

    /**
     * Destroys vis identifier object instance.
     */
    ~VISIdentifier() override;

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

    std::shared_ptr<WSClientItf>                                mWsClientPtr;
    iam::identhandler::SubjectsObserverItf*                     mSubjectsObserver = nullptr;
    VISSubscriptions                                            mSubscriptions;
    StaticString<cSystemIDLen>                                  mSystemId;
    StaticString<cUnitModelLen>                                 mUnitModel;
    StaticArray<StaticString<cSubjectIDLen>, cMaxSubjectIDSize> mSubjects;
    std::thread                                                 mHandleConnectionThread;
    Poco::Event                                                 mWSClientIsConnected;
    Poco::Event                                                 mStopHandleSubjectsChangedThread;
    std::mutex                                                  mMutex;
};

} // namespace aos::iam::visidentifier

#endif
