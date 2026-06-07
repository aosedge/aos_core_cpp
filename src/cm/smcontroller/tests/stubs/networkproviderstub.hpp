/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_NETWORKPROVIDERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_NETWORKPROVIDERSTUB_HPP_

#include <core/common/networkmanager/itf/networkprovider.hpp>

namespace aos::cm::smcontroller {

class NetworkProviderStub : public aos::networkmanager::NetworkProviderItf {
public:
    void SetNodeNetworkParams(const NetworkParams& params) { mNodeNetworkParams = params; }
    void SetInstanceNetworkParams(const InstanceNetworkAllocation& params) { mInstanceNetworkParams = params; }
    void SetError(Error err) { mError = err; }

    Error GetNodeNetworkParams(const String& networkID, const String& nodeID, NetworkParams& result) override
    {
        mLastNetworkID = networkID;
        mLastNodeID    = nodeID;

        if (!mError.IsNone()) {
            return mError;
        }

        result = mNodeNetworkParams;

        return ErrorEnum::eNone;
    }

    Error AllocateInstanceNetwork(const InstanceIdent& instance, const String& networkID, const String& nodeID,
        [[maybe_unused]] const UpdateItemNetworkParams& serviceData, InstanceNetworkAllocation& result) override
    {
        mLastInstance  = instance;
        mLastNetworkID = networkID;
        mLastNodeID    = nodeID;

        if (!mError.IsNone()) {
            return mError;
        }

        result = mInstanceNetworkParams;

        return ErrorEnum::eNone;
    }

    Error ReleaseInstanceNetwork(const InstanceIdent& instance, const String& nodeID) override
    {
        mLastInstance = instance;
        mLastNodeID   = nodeID;

        return mError;
    }

    Error ReleaseNodeNetwork(const String& networkID, const String& nodeID) override
    {
        mLastNetworkID = networkID;
        mLastNodeID    = nodeID;

        return mError;
    }

    Error SyncNetworkState(const String& nodeID, const Array<InstanceNetworkStateInfo>& instances) override
    {
        mLastNodeID         = nodeID;
        mSyncInstancesCount = instances.Size();

        return mError;
    }

    StaticString<cIDLen> mLastNetworkID;
    StaticString<cIDLen> mLastNodeID;
    InstanceIdent        mLastInstance;
    size_t               mSyncInstancesCount {};

private:
    NetworkParams             mNodeNetworkParams;
    InstanceNetworkAllocation mInstanceNetworkParams;
    Error                     mError;
};

} // namespace aos::cm::smcontroller

#endif
