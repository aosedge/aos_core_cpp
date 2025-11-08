/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_SMINFORECEIVERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_SMINFORECEIVERSTUB_HPP_

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <core/cm/nodeinfoprovider/itf/sminforeceiver.hpp>

namespace aos::cm::nodeinfoprovider {

/**
 * SM info receiver stub.
 */
class SMInfoReceiverStub : public SMInfoReceiverItf {
public:
    void OnSMConnected(const String& nodeID) override
    {
        std::lock_guard lock {mMutex};

        mConnectedNodes.push_back(std::string(nodeID.CStr()));
        mCV.notify_all();
    }

    void OnSMDisconnected(const String& nodeID, const Error& err) override
    {
        std::lock_guard lock {mMutex};
        (void)err;

        auto it = std::find(mConnectedNodes.begin(), mConnectedNodes.end(), std::string(nodeID.CStr()));

        if (it == mConnectedNodes.end()) {
            throw std::runtime_error("node not found: " + std::string(nodeID.CStr()));
        }

        mConnectedNodes.erase(it);
        mCV.notify_all();
    }

    Error OnSMInfoReceived(const SMInfo& info) override
    {
        std::lock_guard lock {mMutex};

        for (auto& existingInfo : mSMInfos) {
            if (existingInfo.mNodeID == info.mNodeID) {
                existingInfo = info;
                mCV.notify_all();

                return ErrorEnum::eNone;
            }
        }

        mSMInfos.push_back(info);
        mCV.notify_all();

        return ErrorEnum::eNone;
    }

    Error WaitConnect(const String& nodeID)
    {
        std::unique_lock lock {mMutex};

        auto isConnected = [this, &nodeID]() {
            return std::find(mConnectedNodes.begin(), mConnectedNodes.end(), std::string(nodeID.CStr()))
                != mConnectedNodes.end();
        };

        bool connected = mCV.wait_for(lock, cDefaultTimeout, isConnected);

        if (!connected) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait connect timeout"));
        }

        return ErrorEnum::eNone;
    }

    Error WaitDisconnect(const String& nodeID)
    {
        std::unique_lock lock {mMutex};

        auto isDisconnected = [this, &nodeID]() {
            return std::find(mConnectedNodes.begin(), mConnectedNodes.end(), std::string(nodeID.CStr()))
                == mConnectedNodes.end();
        };

        bool disconnected = mCV.wait_for(lock, cDefaultTimeout, isDisconnected);

        if (!disconnected) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait disconnect timeout"));
        }

        return ErrorEnum::eNone;
    }

    Error WaitSMInfo(const String& nodeID)
    {
        std::unique_lock lock {mMutex};

        auto hasSMInfo = [this, &nodeID]() {
            auto it = std::find_if(
                mSMInfos.begin(), mSMInfos.end(), [&nodeID](const SMInfo& info) { return info.mNodeID == nodeID; });

            return it != mSMInfos.end();
        };

        bool received = mCV.wait_for(lock, cDefaultTimeout, hasSMInfo);

        if (!received) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait SM info timeout"));
        }

        return ErrorEnum::eNone;
    }

    bool HasSMInfo(const String& nodeID) const
    {
        std::lock_guard lock {mMutex};

        auto it = std::find_if(
            mSMInfos.begin(), mSMInfos.end(), [&nodeID](const SMInfo& info) { return info.mNodeID == nodeID; });

        return it != mSMInfos.end();
    }

    bool IsNodeConnected(const String& nodeID) const
    {
        std::lock_guard lock {mMutex};

        auto it = std::find(mConnectedNodes.begin(), mConnectedNodes.end(), std::string(nodeID.CStr()));

        return it != mConnectedNodes.end();
    }

    SMInfo GetSMInfo(const String& nodeID) const
    {
        std::lock_guard lock {mMutex};

        auto it = std::find_if(
            mSMInfos.begin(), mSMInfos.end(), [&nodeID](const SMInfo& info) { return info.mNodeID == nodeID; });

        if (it == mSMInfos.end()) {
            return SMInfo {};
        }

        return *it;
    }

private:
    static constexpr auto cDefaultTimeout = std::chrono::seconds(10);

    std::vector<std::string> mConnectedNodes;
    std::vector<SMInfo>      mSMInfos;
    mutable std::mutex       mMutex;
    std::condition_variable  mCV;
};

} // namespace aos::cm::nodeinfoprovider

#endif
