/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_CLOUDCONNECTIONSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_CLOUDCONNECTIONSTUB_HPP_

#include <core/common/cloudconnection/itf/cloudconnection.hpp>

namespace aos::cm::smcontroller {

/**
 * Cloud connection stub for testing purposes.
 */
class CloudConnectionStub : public cloudconnection::CloudConnectionItf {
public:
    Error SubscribeListener(cloudconnection::ConnectionListenerItf& listener) override
    {
        mListener = &listener;

        return ErrorEnum::eNone;
    }

    Error UnsubscribeListener(cloudconnection::ConnectionListenerItf& listener) override
    {
        if (mListener == &listener) {
            mListener = nullptr;
        }

        return ErrorEnum::eNone;
    }

    /**
     * Triggers OnConnect event on the subscribed listener.
     */
    void TriggerConnect()
    {
        mIsConnected = true;

        if (mListener) {
            mListener->OnConnect();
        }
    }

    /**
     * Triggers OnDisconnect event on the subscribed listener.
     */
    void TriggerDisconnect()
    {
        mIsConnected = false;

        if (mListener) {
            mListener->OnDisconnect();
        }
    }

    bool IsConnected() const override { return mIsConnected; }

private:
    cloudconnection::ConnectionListenerItf* mListener {};
    bool                                    mIsConnected {};
};

} // namespace aos::cm::smcontroller

#endif
