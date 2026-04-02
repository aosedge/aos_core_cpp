/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <cm/networkmanager/ipsubnet.hpp>
#include <cm/networkmanager/netpool.hpp>
#include <cm/networkmanager/networkmanager.hpp>

#include <common/network/utils.hpp>
#include <common/utils/exception.hpp>

#include <core/common/networkmanager/tests/mocks/pendingupdatehandlermock.hpp>

#include "mocks/dnsservermock.hpp"
#include "mocks/randommock.hpp"
#include "mocks/storagemock.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

namespace aos::cm::networkmanager {

class CMNetworkManagerTest : public Test {
public:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mNetworkManager = std::make_unique<NetworkManager>();
        mStorage        = std::make_unique<StrictMock<MockStorage>>();
        mRandom         = std::make_unique<StrictMock<crypto::MockRandom>>();
        mDNSServer      = std::make_unique<StrictMock<MockDNSServer>>();

        // Default: no pending connections in DB
        EXPECT_CALL(*mStorage, GetAllPendingConnections(_)).WillRepeatedly(Return(ErrorEnum::eNone));
        EXPECT_CALL(*mStorage, AddPendingConnection(_)).WillRepeatedly(Return(ErrorEnum::eNone));
        EXPECT_CALL(*mStorage, RemovePendingConnection(_)).WillRepeatedly(Return(ErrorEnum::eNone));
        EXPECT_CALL(*mStorage, RemovePendingConnections(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    }

    void TearDown() override { }

protected:
    std::unique_ptr<NetworkManager>                 mNetworkManager;
    std::unique_ptr<StrictMock<MockStorage>>        mStorage;
    std::unique_ptr<StrictMock<crypto::MockRandom>> mRandom;
    std::unique_ptr<StrictMock<MockDNSServer>>      mDNSServer;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CMNetworkManagerTest, GetNodeNetworkParams_NewNetwork)
{
    String networkID = "network1";
    String nodeID    = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mRandom, RandInt(_)).WillOnce(Return(RetWithError<uint64_t>(1000u, ErrorEnum::eNone)));

    EXPECT_CALL(*mStorage, AddNetwork(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, AddHost(_, _)).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    NetworkParams result;

    err = mNetworkManager->GetNodeNetworkParams(networkID, nodeID, result);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result.mNetworkID, "network1");
    EXPECT_FALSE(result.mSubnet.IsEmpty());
    EXPECT_FALSE(result.mIP.IsEmpty());
    EXPECT_EQ(result.mVlanID, 1000);
}

TEST_F(CMNetworkManagerTest, GetNodeNetworkParams_ExistingNetwork_NewNode)
{
    String networkID = "network1";
    String nodeID    = "node2";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, AddHost(String("network1"), _)).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    NetworkParams result;

    err = mNetworkManager->GetNodeNetworkParams(networkID, nodeID, result);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result.mNetworkID, "network1");
    EXPECT_EQ(result.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result.mVlanID, 1000);
    EXPECT_FALSE(result.mIP.IsEmpty());
    EXPECT_NE(result.mIP, "172.17.0.1");
}

TEST_F(CMNetworkManagerTest, GetNodeNetworkParams_ExistingNetwork_ExistingNode)
{
    String networkID = "network1";
    String nodeID    = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _)).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    NetworkParams result;

    err = mNetworkManager->GetNodeNetworkParams(networkID, nodeID, result);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result.mNetworkID, "network1");
    EXPECT_EQ(result.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result.mIP, "172.17.0.1");
    EXPECT_EQ(result.mVlanID, 1000);
}

TEST_F(CMNetworkManagerTest, GetNodeNetworkParams_StorageError)
{
    String networkID = "network1";
    String nodeID    = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mRandom, RandInt(_)).WillOnce(Return(RetWithError<uint64_t>(1000u, ErrorEnum::eNone)));

    EXPECT_CALL(*mStorage, AddNetwork(_)).WillOnce(Return(Error(ErrorEnum::eRuntime, "Storage error")));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    NetworkParams result;

    err = mNetworkManager->GetNodeNetworkParams(networkID, nodeID, result);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, GetNodeNetworkParams_RandomError)
{
    String networkID = "network1";
    String nodeID    = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mRandom, RandInt(_))
        .WillOnce(Return(RetWithError<uint64_t>(0u, Error(ErrorEnum::eRuntime, "Random error"))));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    NetworkParams result;

    err = mNetworkManager->GetNodeNetworkParams(networkID, nodeID, result);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_NewInstance_Success)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network1";
    String nodeID    = "node1";

    UpdateItemNetworkParams   instanceData;
    InstanceNetworkAllocation result1;
    InstanceNetworkAllocation result2;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.2";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, AddInstance(_)).Times(2);

    EXPECT_CALL(*mDNSServer, GetIP()).WillOnce(Return("8.8.8.8")).WillOnce(Return("1.1.1.1"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent, networkID, nodeID, instanceData, result1);

    EXPECT_TRUE(err.IsNone());
    EXPECT_EQ(result1.mNetworkID, "network1");
    EXPECT_EQ(result1.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result1.mDNSServers.Size(), 1);
    EXPECT_EQ(result1.mDNSServers[0], "8.8.8.8");
    EXPECT_FALSE(result1.mIP.IsEmpty());
    EXPECT_NE(result1.mIP, "172.17.0.2");

    InstanceIdent instanceIdent2;
    instanceIdent2.mItemID    = "service2";
    instanceIdent2.mSubjectID = "subject2";
    instanceIdent2.mInstance  = 1;

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent2, networkID, nodeID, instanceData, result2);
    EXPECT_TRUE(err.IsNone());

    EXPECT_NE(result1.mIP, result2.mIP);
    EXPECT_EQ(result1.mSubnet, result2.mSubnet);
    EXPECT_EQ(result1.mNetworkID, result2.mNetworkID);
    EXPECT_EQ(result2.mDNSServers.Size(), 1);
    EXPECT_EQ(result2.mDNSServers[0], "1.1.1.1");
}

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_ExistingInstance_Success)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network1";
    String nodeID    = "node1";

    UpdateItemNetworkParams   instanceData;
    InstanceNetworkAllocation result;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _))
        .WillOnce(Invoke([&instanceIdent](const String&, const String&, Array<Instance>& instances) -> Error {
            Instance instance;
            instance.mNetworkID     = "network1";
            instance.mNodeID        = "node1";
            instance.mInstanceIdent = instanceIdent;
            instance.mIP            = "172.17.0.10";
            instances.PushBack(instance);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent, networkID, nodeID, instanceData, result);

    EXPECT_TRUE(err.IsNone());
    EXPECT_EQ(result.mNetworkID, "network1");
    EXPECT_EQ(result.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result.mIP, "172.17.0.10");
}

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_NetworkNotFound_Error)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network_nonexistent";
    String nodeID    = "node1";

    UpdateItemNetworkParams   instanceData;
    InstanceNetworkAllocation result;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent, networkID, nodeID, instanceData, result);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_NodeNotFound_Error)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network1";
    String nodeID    = "node_nonexistent";

    UpdateItemNetworkParams   instanceData;
    InstanceNetworkAllocation result;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _)).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent, networkID, nodeID, instanceData, result);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_DynamicDNS)
{
    InstanceIdent instanceIdent1;
    instanceIdent1.mItemID    = "service1";
    instanceIdent1.mSubjectID = "subject1";
    instanceIdent1.mInstance  = 1;

    InstanceIdent instanceIdent2;
    instanceIdent2.mItemID    = "service2";
    instanceIdent2.mSubjectID = "subject2";
    instanceIdent2.mInstance  = 0;

    String networkID = "network1";
    String nodeID    = "node1";

    UpdateItemNetworkParams instanceData1;
    instanceData1.mHosts.PushBack("custom1.example.com");
    instanceData1.mHosts.PushBack("api1.example.com");
    instanceData1.mHosts.PushBack("1.subject1.service1");
    instanceData1.mHosts.PushBack("1.subject1.service1.network1");

    UpdateItemNetworkParams instanceData2;
    instanceData2.mHosts.PushBack("custom2.example.com");
    instanceData2.mHosts.PushBack("0.subject2.service2");
    instanceData2.mHosts.PushBack("0.subject2.service2.network1");
    instanceData2.mHosts.PushBack("subject2.service2");
    instanceData2.mHosts.PushBack("subject2.service2.network1");

    InstanceNetworkAllocation result1, result2;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.2";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, AddInstance(_)).Times(2);

    EXPECT_CALL(*mDNSServer, GetIP()).WillOnce(Return("8.8.8.8")).WillOnce(Return("1.1.1.1"));

    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_))
        .WillOnce(Return(ErrorEnum::eNone))
        .WillOnce(Invoke([&](const HostsMap& hosts) -> Error {
            EXPECT_EQ(hosts.size(), 2);

            auto it1 = hosts.find(result1.mIP.CStr());
            auto it2 = hosts.find(result2.mIP.CStr());

            EXPECT_NE(it1, hosts.end());
            EXPECT_NE(it2, hosts.end());

            if (it1 != hosts.end()) {
                const auto& hosts1 = it1->second;
                EXPECT_TRUE(std::find(hosts1.begin(), hosts1.end(), "custom1.example.com") != hosts1.end());
                EXPECT_TRUE(std::find(hosts1.begin(), hosts1.end(), "api1.example.com") != hosts1.end());
                EXPECT_TRUE(std::find(hosts1.begin(), hosts1.end(), "1.subject1.service1") != hosts1.end());
                EXPECT_TRUE(std::find(hosts1.begin(), hosts1.end(), "1.subject1.service1.network1") != hosts1.end());
            }

            if (it2 != hosts.end()) {
                const auto& hosts2 = it2->second;
                EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "custom2.example.com") != hosts2.end());
                EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "0.subject2.service2") != hosts2.end());
                EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "0.subject2.service2.network1") != hosts2.end());
                EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "subject2.service2") != hosts2.end());
                EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "subject2.service2.network1") != hosts2.end());
            }

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mDNSServer, Restart()).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent1, networkID, nodeID, instanceData1, result1);
    EXPECT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent2, networkID, nodeID, instanceData2, result2);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_CrossNetworkFirewall_Success)
{
    InstanceIdent instanceIdent1;
    instanceIdent1.mItemID    = "service1";
    instanceIdent1.mSubjectID = "subject1";
    instanceIdent1.mInstance  = 1;

    InstanceIdent instanceIdent2;
    instanceIdent2.mItemID    = "service2";
    instanceIdent2.mSubjectID = "subject2";
    instanceIdent2.mInstance  = 1;

    String networkID1 = "network1";
    String networkID2 = "network2";
    String nodeID     = "node1";

    UpdateItemNetworkParams instanceData1;
    instanceData1.mExposedPorts.PushBack("8080/tcp");
    instanceData1.mExposedPorts.PushBack("9090/udp");

    UpdateItemNetworkParams instanceData2;
    instanceData2.mAllowedConnections.PushBack("service1/8080/tcp");
    instanceData2.mAllowedConnections.PushBack("service1/9090/udp");

    InstanceNetworkAllocation result1, result2;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network1;
        network1.mNetworkID = "network1";
        network1.mSubnet    = "172.17.0.0/16";
        network1.mVlanID    = 1000;
        networks.PushBack(network1);

        Network network2;
        network2.mNetworkID = "network2";
        network2.mSubnet    = "172.18.0.0/16";
        network2.mVlanID    = 2000;
        networks.PushBack(network2);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(_, _))
        .WillRepeatedly(Invoke([](const String& networkID, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            if (networkID == "network1") {
                host.mIP = "172.17.0.2";
            } else {
                host.mIP = "172.18.0.2";
            }
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, AddInstance(_)).Times(2);

    EXPECT_CALL(*mDNSServer, GetIP()).WillOnce(Return("8.8.8.8")).WillOnce(Return("1.1.1.1"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent1, networkID1, nodeID, instanceData1, result1);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result1.mNetworkID, "network1");
    EXPECT_EQ(result1.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result1.mDNSServers.Size(), 1);
    EXPECT_EQ(result1.mDNSServers[0], "8.8.8.8");
    EXPECT_FALSE(result1.mIP.IsEmpty());
    EXPECT_NE(result1.mIP, "172.17.0.2");
    EXPECT_EQ(result1.mFirewallRules.Size(), 0);

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent2, networkID2, nodeID, instanceData2, result2);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result2.mNetworkID, "network2");
    EXPECT_EQ(result2.mSubnet, "172.18.0.0/16");
    EXPECT_EQ(result2.mDNSServers.Size(), 1);
    EXPECT_EQ(result2.mDNSServers[0], "1.1.1.1");
    EXPECT_FALSE(result2.mIP.IsEmpty());
    EXPECT_NE(result2.mIP, "172.18.0.2");

    EXPECT_EQ(result2.mFirewallRules.Size(), 2);

    if (result2.mFirewallRules.Size() >= 2) {
        EXPECT_EQ(result2.mFirewallRules[0].mDstIP, result1.mIP);
        EXPECT_EQ(result2.mFirewallRules[0].mSrcIP, result2.mIP);
        EXPECT_EQ(result2.mFirewallRules[0].mProto, "tcp");
        EXPECT_EQ(result2.mFirewallRules[0].mDstPort, "8080");

        EXPECT_EQ(result2.mFirewallRules[1].mDstIP, result1.mIP);
        EXPECT_EQ(result2.mFirewallRules[1].mSrcIP, result2.mIP);
        EXPECT_EQ(result2.mFirewallRules[1].mProto, "udp");
        EXPECT_EQ(result2.mFirewallRules[1].mDstPort, "9090");
    }
}

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_MigrateInstance_Success)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network1";
    String nodeID1   = "node1";
    String nodeID2   = "node2";

    UpdateItemNetworkParams   instanceData;
    InstanceNetworkAllocation result1;
    InstanceNetworkAllocation result2;
    InstanceNetworkAllocation result3;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host1;
            host1.mNodeID = "node1";
            host1.mIP     = "172.17.0.1";
            hosts.PushBack(host1);

            Host host2;
            host2.mNodeID = "node2";
            host2.mIP     = "172.17.0.2";
            hosts.PushBack(host2);

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, AddInstance(_)).Times(2);
    EXPECT_CALL(*mStorage, RemoveNetworkInstance(instanceIdent)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mDNSServer, GetIP()).WillOnce(Return("8.8.8.8"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).Times(3).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).Times(3).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent, networkID, nodeID1, instanceData, result1);
    ASSERT_TRUE(err.IsNone());
    EXPECT_FALSE(result1.mIP.IsEmpty());
    EXPECT_EQ(result1.mDNSServers.Size(), 1);
    EXPECT_EQ(result1.mDNSServers[0], "8.8.8.8");

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent, networkID, nodeID2, instanceData, result2);
    ASSERT_TRUE(err.IsNone());

    EXPECT_EQ(result2.mIP, result1.mIP);
    EXPECT_EQ(result2.mDNSServers.Size(), 1);
    EXPECT_EQ(result2.mDNSServers[0], "8.8.8.8");
    EXPECT_EQ(result2.mNetworkID, "network1");
    EXPECT_EQ(result2.mSubnet, "172.17.0.0/16");

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent, networkID, nodeID2, instanceData, result3);
    ASSERT_TRUE(err.IsNone());
    EXPECT_EQ(result3.mIP, result1.mIP);
}

TEST_F(CMNetworkManagerTest, ReleaseInstanceNetwork_Success)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String nodeID = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _))
        .WillOnce(Invoke([&instanceIdent](const String&, const String&, Array<Instance>& instances) -> Error {
            Instance instance;
            instance.mNetworkID     = "network1";
            instance.mNodeID        = "node1";
            instance.mInstanceIdent = instanceIdent;
            instance.mIP            = "172.17.0.10";
            instances.PushBack(instance);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, RemoveNetworkInstance(instanceIdent)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, AddInstance(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mDNSServer, GetIP()).WillOnce(Return("8.8.8.8"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).Times(2).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->ReleaseInstanceNetwork(instanceIdent, nodeID);
    EXPECT_TRUE(err.IsNone());

    InstanceNetworkAllocation result;
    UpdateItemNetworkParams   instanceData;

    err = mNetworkManager->AllocateInstanceNetwork(instanceIdent, "network1", nodeID, instanceData, result);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result.mNetworkID, "network1");
    EXPECT_FALSE(result.mIP.IsEmpty());
    EXPECT_EQ(result.mDNSServers.Size(), 1);
    EXPECT_EQ(result.mDNSServers[0], "8.8.8.8");
}

TEST_F(CMNetworkManagerTest, ReleaseNodeNetwork_Success)
{
    String networkID = "network1";
    String nodeID1   = "node1";

    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host1;
            host1.mNodeID = "node1";
            host1.mIP     = "172.17.0.1";
            hosts.PushBack(host1);

            Host host2;
            host2.mNodeID = "node2";
            host2.mIP     = "172.17.0.2";
            hosts.PushBack(host2);

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _))
        .WillOnce(Invoke([&instanceIdent](const String&, const String&, Array<Instance>& instances) -> Error {
            Instance instance;
            instance.mNetworkID     = "network1";
            instance.mNodeID        = "node1";
            instance.mInstanceIdent = instanceIdent;
            instance.mIP            = "172.17.0.10";
            instances.PushBack(instance);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node2"), _)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, RemoveNetworkInstance(instanceIdent)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, RemoveHost(String("network1"), String("node1"))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->ReleaseNodeNetwork(networkID, nodeID1);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, ReleaseNodeNetwork_EmptyNetwork)
{
    String networkID = "network1";
    String nodeID    = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("network1"), String("node1"), _)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, RemoveHost(String("network1"), String("node1"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, RemoveNetwork(String("network1"))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->ReleaseNodeNetwork(networkID, nodeID);
    EXPECT_TRUE(err.IsNone());
}

/***********************************************************************************************************************
 * Deferred Firewall Tests
 **********************************************************************************************************************/

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_MissingDependency_ReturnsPartialRules)
{
    InstanceIdent identA;
    identA.mItemID    = "serviceA";
    identA.mSubjectID = "subject1";
    identA.mInstance  = 1;

    String networkID = "network1";
    String nodeID    = "node1";

    UpdateItemNetworkParams dataA;
    dataA.mAllowedConnections.PushBack("serviceB/8080/tcp");
    dataA.mExposedPorts.PushBack("80/tcp");

    InstanceNetworkAllocation resultA;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "network1";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, AddInstance(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, GetIP()).WillOnce(Return("8.8.8.8"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(identA, networkID, nodeID, dataA, resultA);
    EXPECT_TRUE(err.IsNone());
    EXPECT_TRUE(resultA.mFirewallRules.IsEmpty());
}

TEST_F(CMNetworkManagerTest, AllocateInstanceNetwork_ResolvesPending_PushesNotification)
{
    PendingUpdateHandlerMock handler;

    InstanceIdent identA;
    identA.mItemID    = "serviceA";
    identA.mSubjectID = "subject1";
    identA.mInstance  = 1;

    InstanceIdent identB;
    identB.mItemID    = "serviceB";
    identB.mSubjectID = "subject1";
    identB.mInstance  = 1;

    String networkID1 = "network1";
    String networkID2 = "network2";
    String nodeID1    = "node1";
    String nodeID2    = "node2";

    UpdateItemNetworkParams dataA;
    dataA.mAllowedConnections.PushBack("serviceB/8080/tcp");

    UpdateItemNetworkParams dataB;
    dataB.mExposedPorts.PushBack("8080/tcp");

    InstanceNetworkAllocation resultA;
    InstanceNetworkAllocation resultB;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network net1;
        net1.mNetworkID = "network1";
        net1.mSubnet    = "172.17.0.0/16";
        net1.mVlanID    = 1000;
        networks.PushBack(net1);

        Network net2;
        net2.mNetworkID = "network2";
        net2.mSubnet    = "172.18.0.0/16";
        net2.mVlanID    = 1001;
        networks.PushBack(net2);

        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetHosts(String("network2"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node2";
            host.mIP     = "172.18.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, AddInstance(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, GetIP()).WillRepeatedly(Return("8.8.8.8"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer, &handler);
    ASSERT_TRUE(err.IsNone());

    // Allocate A on network1/node1 — B doesn't exist → partial rules
    err = mNetworkManager->AllocateInstanceNetwork(identA, networkID1, nodeID1, dataA, resultA);
    EXPECT_TRUE(err.IsNone());
    EXPECT_TRUE(resultA.mFirewallRules.IsEmpty());

    // Allocate B on network2/node2 — resolves pending for A
    EXPECT_CALL(handler, OnPendingFirewallUpdate(_, _))
        .WillOnce(Invoke([&](const String& /*nodeID*/, const aos::networkmanager::PendingFirewallUpdate& update) {
            EXPECT_EQ(update.mInstanceIdent, identA);
            EXPECT_EQ(update.mFirewallRules.Size(), 1);
            EXPECT_EQ(update.mFirewallRules[0].mDstPort, "8080");
            EXPECT_EQ(update.mFirewallRules[0].mProto, "tcp");
        }));

    err = mNetworkManager->AllocateInstanceNetwork(identB, networkID2, nodeID2, dataB, resultB);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, ReleaseInstanceNetwork_CleansPending)
{
    PendingUpdateHandlerMock handler;

    InstanceIdent identA;
    identA.mItemID    = "serviceA";
    identA.mSubjectID = "subject1";
    identA.mInstance  = 1;

    InstanceIdent identB;
    identB.mItemID    = "serviceB";
    identB.mSubjectID = "subject1";
    identB.mInstance  = 1;

    String networkID1 = "network1";
    String networkID2 = "network2";
    String nodeID1    = "node1";
    String nodeID2    = "node2";

    UpdateItemNetworkParams dataA;
    dataA.mAllowedConnections.PushBack("serviceB/8080/tcp");

    UpdateItemNetworkParams dataB;
    dataB.mExposedPorts.PushBack("8080/tcp");

    InstanceNetworkAllocation resultA;
    InstanceNetworkAllocation resultB;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network net1;
        net1.mNetworkID = "network1";
        net1.mSubnet    = "172.17.0.0/16";
        net1.mVlanID    = 1000;
        networks.PushBack(net1);

        Network net2;
        net2.mNetworkID = "network2";
        net2.mSubnet    = "172.18.0.0/16";
        net2.mVlanID    = 1001;
        networks.PushBack(net2);

        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetHosts(String("network2"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node2";
            host.mIP     = "172.18.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, AddInstance(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, RemoveNetworkInstance(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, GetIP()).WillRepeatedly(Return("8.8.8.8"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer, &handler);
    ASSERT_TRUE(err.IsNone());

    // Allocate A with pending on B
    err = mNetworkManager->AllocateInstanceNetwork(identA, networkID1, nodeID1, dataA, resultA);
    ASSERT_TRUE(err.IsNone());

    // Release A — pending cleaned
    err = mNetworkManager->ReleaseInstanceNetwork(identA, nodeID1);
    EXPECT_TRUE(err.IsNone());

    // Allocate B — should NOT trigger notification (pending was cleaned)
    EXPECT_CALL(handler, OnPendingFirewallUpdate(_, _)).Times(0);

    err = mNetworkManager->AllocateInstanceNetwork(identB, networkID2, nodeID2, dataB, resultB);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, SyncNetworkState_ReleasesStaleInstances)
{
    InstanceIdent identA;
    identA.mItemID    = "serviceA";
    identA.mSubjectID = "subject1";
    identA.mInstance  = 1;

    InstanceIdent identB;
    identB.mItemID    = "serviceB";
    identB.mSubjectID = "subject1";
    identB.mInstance  = 1;

    String networkID = "network1";
    String nodeID    = "node1";

    UpdateItemNetworkParams data;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network net;
        net.mNetworkID = "network1";
        net.mSubnet    = "172.17.0.0/16";
        net.mVlanID    = 1000;
        networks.PushBack(net);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(_, _)).WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
        Host host;
        host.mNodeID = "node1";
        host.mIP     = "172.17.0.1";
        hosts.PushBack(host);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, AddInstance(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, RemoveNetworkInstance(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, GetIP()).WillRepeatedly(Return("8.8.8.8"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    InstanceNetworkAllocation resultA, resultB;
    err = mNetworkManager->AllocateInstanceNetwork(identA, networkID, nodeID, data, resultA);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->AllocateInstanceNetwork(identB, networkID, nodeID, data, resultB);
    ASSERT_TRUE(err.IsNone());

    StaticArray<InstanceNetworkStateInfo, cMaxNumInstances> smState;
    smState.EmplaceBack(identA, networkID, resultA.mIP, resultA.mFirewallRules);

    err = mNetworkManager->SyncNetworkState(nodeID, smState);
    EXPECT_TRUE(err.IsNone());

    err = mNetworkManager->ReleaseInstanceNetwork(identB, nodeID);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, SyncNetworkState_ReResolvePendingConnections)
{
    PendingUpdateHandlerMock handler;

    InstanceIdent identA;
    identA.mItemID    = "serviceA";
    identA.mSubjectID = "subject1";
    identA.mInstance  = 1;

    InstanceIdent identB;
    identB.mItemID    = "serviceB";
    identB.mSubjectID = "subject1";
    identB.mInstance  = 1;

    String networkID1 = "network1";
    String networkID2 = "network2";
    String nodeID1    = "node1";
    String nodeID2    = "node2";

    UpdateItemNetworkParams dataA;
    dataA.mAllowedConnections.PushBack("serviceB/8080/tcp");

    UpdateItemNetworkParams dataB;
    dataB.mExposedPorts.PushBack("8080/tcp");

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network net1;
        net1.mNetworkID = "network1";
        net1.mSubnet    = "172.17.0.0/16";
        net1.mVlanID    = 1000;
        networks.PushBack(net1);

        Network net2;
        net2.mNetworkID = "network2";
        net2.mSubnet    = "172.18.0.0/16";
        net2.mVlanID    = 1001;
        networks.PushBack(net2);

        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetHosts(String("network2"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node2";
            host.mIP     = "172.18.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, AddInstance(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, GetIP()).WillRepeatedly(Return("8.8.8.8"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer, &handler);
    ASSERT_TRUE(err.IsNone());

    InstanceNetworkAllocation resultA;
    err = mNetworkManager->AllocateInstanceNetwork(identA, networkID1, nodeID1, dataA, resultA);
    ASSERT_TRUE(err.IsNone());
    EXPECT_TRUE(resultA.mFirewallRules.IsEmpty());

    EXPECT_CALL(handler, OnPendingFirewallUpdate(_, _)).Times(1);

    InstanceNetworkAllocation resultB;
    err = mNetworkManager->AllocateInstanceNetwork(identB, networkID2, nodeID2, dataB, resultB);
    ASSERT_TRUE(err.IsNone());

    StaticArray<InstanceNetworkStateInfo, cMaxNumInstances> smState;
    StaticArray<FirewallRule, cMaxNumFirewallRules>         emptyRules;
    smState.EmplaceBack(identA, networkID1, resultA.mIP, emptyRules);

    EXPECT_CALL(*mStorage, GetAllPendingConnections(_))
        .WillRepeatedly(Invoke([&](Array<PendingConnection>& connections) -> Error {
            PendingConnection pending;
            pending.mRequesterIdent  = identA;
            pending.mNodeID          = nodeID1;
            pending.mNetworkID       = networkID1;
            pending.mRequesterIP     = resultA.mIP;
            pending.mRequesterSubnet = resultA.mSubnet;
            pending.mTargetItemID    = "serviceB";
            pending.mPort            = "8080";
            pending.mProtocol        = "tcp";
            connections.PushBack(pending);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(handler, OnPendingFirewallUpdate(_, _))
        .WillOnce(Invoke([&](const String& /*nodeID*/, const aos::networkmanager::PendingFirewallUpdate& update) {
            EXPECT_EQ(update.mInstanceIdent, identA);
            EXPECT_EQ(update.mFirewallRules.Size(), 1);
        }));

    err = mNetworkManager->SyncNetworkState(nodeID1, smState);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, SyncNetworkState_ConfirmedPendingCleanedFromDB)
{
    PendingUpdateHandlerMock handler;

    InstanceIdent identA;
    identA.mItemID    = "serviceA";
    identA.mSubjectID = "subject1";
    identA.mInstance  = 1;

    InstanceIdent identB;
    identB.mItemID    = "serviceB";
    identB.mSubjectID = "subject1";
    identB.mInstance  = 1;

    String networkID1 = "network1";
    String networkID2 = "network2";
    String nodeID1    = "node1";
    String nodeID2    = "node2";

    UpdateItemNetworkParams dataA;
    dataA.mAllowedConnections.PushBack("serviceB/8080/tcp");

    UpdateItemNetworkParams dataB;
    dataB.mExposedPorts.PushBack("8080/tcp");

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network net1;
        net1.mNetworkID = "network1";
        net1.mSubnet    = "172.17.0.0/16";
        net1.mVlanID    = 1000;
        networks.PushBack(net1);

        Network net2;
        net2.mNetworkID = "network2";
        net2.mSubnet    = "172.18.0.0/16";
        net2.mVlanID    = 1001;
        networks.PushBack(net2);

        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("network1"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetHosts(String("network2"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node2";
            host.mIP     = "172.18.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, AddInstance(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, GetIP()).WillRepeatedly(Return("8.8.8.8"));
    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mDNSServer, Restart()).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mDNSServer, &handler);
    ASSERT_TRUE(err.IsNone());

    InstanceNetworkAllocation resultA;
    err = mNetworkManager->AllocateInstanceNetwork(identA, networkID1, nodeID1, dataA, resultA);
    ASSERT_TRUE(err.IsNone());

    EXPECT_CALL(handler, OnPendingFirewallUpdate(_, _)).Times(AnyNumber());

    InstanceNetworkAllocation resultB;
    err = mNetworkManager->AllocateInstanceNetwork(identB, networkID2, nodeID2, dataB, resultB);
    ASSERT_TRUE(err.IsNone());

    StaticArray<InstanceNetworkStateInfo, cMaxNumInstances> smState;

    StaticArray<FirewallRule, cMaxNumFirewallRules> fwRules;
    FirewallRule                                    rule;
    rule.mDstIP   = resultB.mIP;
    rule.mDstPort = "8080";
    rule.mProto   = "tcp";
    rule.mSrcIP   = resultA.mIP;
    fwRules.PushBack(rule);

    smState.EmplaceBack(identA, networkID1, resultA.mIP, fwRules);

    EXPECT_CALL(*mStorage, GetAllPendingConnections(_))
        .WillRepeatedly(Invoke([&](Array<PendingConnection>& connections) -> Error {
            PendingConnection pending;
            pending.mRequesterIdent  = identA;
            pending.mNodeID          = nodeID1;
            pending.mNetworkID       = networkID1;
            pending.mRequesterIP     = resultA.mIP;
            pending.mRequesterSubnet = resultA.mSubnet;
            pending.mTargetItemID    = "serviceB";
            pending.mPort            = "8080";
            pending.mProtocol        = "tcp";
            connections.PushBack(pending);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, RemovePendingConnection(_)).WillOnce(Return(ErrorEnum::eNone));

    err = mNetworkManager->SyncNetworkState(nodeID1, smState);
    EXPECT_TRUE(err.IsNone());
}

} // namespace aos::cm::networkmanager
