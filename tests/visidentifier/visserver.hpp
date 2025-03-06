/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VISSERVER_HPP_
#define VISSERVER_HPP_

#include <iostream>
#include <mutex>
#include <thread>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include "visidentifier/vismessage.hpp"

namespace aos::iam::visidentifier {

class VISParams {
public:
    void                     Set(const std::string& key, const std::string& value);
    void                     Set(const std::string& key, const std::vector<std::string>& values);
    std::vector<std::string> Get(const std::string& key);
    static VISParams&        Instance();

private:
    VISParams() = default;

    std::mutex                                      mMutex;
    std::map<std::string, std::vector<std::string>> mMap;
};

class WebSocketRequestHandler : public Poco::Net::HTTPRequestHandler {
public:
    void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override;

private:
    std::string handleGetRequest(const VISMessage& request);
    std::string handleSubscribeRequest(const VISMessage& request);
    std::string handleUnsubscribeAllRequest(const VISMessage& request);
    std::string handleFrame(const std::string& frame);
};

class RequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
public:
    Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest&) override;
};

class VISWebSocketServer {
public:
    static VISWebSocketServer& Instance();
    void Start(const std::string& keyPath, const std::string& certPath, const std::string& uriStr);
    void Stop();
    bool TryWaitServiceStart(const long timeout = 2000);

private:
    VISWebSocketServer() = default;
    void RunServiceThreadF(const std::string keyPath, const std::string certPath, const std::string uriStr);

    std::recursive_mutex mMutex;
    std::thread          mThread;
    Poco::Event          mStopEvent;
    Poco::Event          mStartEvent;
};

} // namespace aos::iam::visidentifier

#endif
