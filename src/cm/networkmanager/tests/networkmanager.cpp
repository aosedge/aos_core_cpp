/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <cm/networkmanager/ipsubnet.hpp>
#include <cm/networkmanager/netpool.hpp>
#include <cm/networkmanager/networkmanager.hpp>

#include <common/network/utils.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/dnsservermock.hpp"
#include "mocks/randommock.hpp"
#include "mocks/sendermock.hpp"
#include "mocks/storagemock.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

namespace aos::cm::networkmanager::tests {

class CMNetworkManagerTest : public Test {
public:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mNetworkManager = std::make_unique<NetworkManager>();
        mStorage        = std::make_unique<StrictMock<MockStorage>>();
        mRandom         = std::make_unique<StrictMock<crypto::MockRandom>>();
        mSender         = std::make_unique<StrictMock<MockSender>>();
        mDNSServer      = std::make_unique<StrictMock<MockDNSServer>>();
    }

    void TearDown() override { }

protected:
    std::unique_ptr<NetworkManager>                 mNetworkManager;
    std::unique_ptr<StrictMock<MockStorage>>        mStorage;
    std::unique_ptr<StrictMock<crypto::MockRandom>> mRandom;
    std::unique_ptr<StrictMock<MockSender>>         mSender;
    std::unique_ptr<StrictMock<MockDNSServer>>      mDNSServer;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CMNetworkManagerTest, UpdateProviderNetwork_Success)
{
    StaticArray<StaticString<cIDLen>, 2> providers;
    providers.PushBack("provider1");
    providers.PushBack("provider2");

    String nodeID = "node1";

    std::vector<NetworkParameters> capturedNetworkParams;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetHosts(_, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mRandom, RandInt(_))
        .WillOnce(Return(RetWithError<uint64_t>(1000u, ErrorEnum::eNone)))
        .WillOnce(Return(RetWithError<uint64_t>(2000u, ErrorEnum::eNone)));

    EXPECT_CALL(*mStorage, AddNetwork(_)).WillOnce(Return(ErrorEnum::eNone)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, AddHost(_, _)).WillOnce(Return(ErrorEnum::eNone)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mSender, SendNetwork(_, _))
        .WillOnce(DoAll(SaveArg<1>(&capturedNetworkParams), Return(ErrorEnum::eNone)));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->UpdateProviderNetwork(providers, nodeID);

    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(capturedNetworkParams.size(), 2);

    for (const auto& networkParams : capturedNetworkParams) {
        EXPECT_FALSE(networkParams.mSubnet.IsEmpty());

        EXPECT_FALSE(networkParams.mIP.IsEmpty());

        EXPECT_TRUE(networkParams.mNetworkID == "provider1" || networkParams.mNetworkID == "provider2");
        EXPECT_GT(networkParams.mVlanID, 0);
        EXPECT_LE(networkParams.mVlanID, 4096);
    }

    StaticArray<StaticString<cIDLen>, 1> updatedProviders;
    updatedProviders.PushBack("provider2");

    EXPECT_CALL(*mStorage, RemoveHost(String("provider1"), nodeID)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, RemoveNetwork(String("provider1"))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mSender, SendNetwork(_, _))
        .WillOnce(Invoke([&](const std::string&, const std::vector<NetworkParameters>& params) -> Error {
            EXPECT_EQ(params.size(), 1);
            auto it = std::find_if(capturedNetworkParams.begin(), capturedNetworkParams.end(),
                [](const NetworkParameters& np) { return np.mNetworkID == "provider2"; });
            EXPECT_NE(it, capturedNetworkParams.end());
            EXPECT_EQ(params[0].mSubnet, it->mSubnet);
            EXPECT_EQ(params[0].mIP, it->mIP);
            EXPECT_EQ(params[0].mVlanID, it->mVlanID);
            EXPECT_EQ(params[0].mNetworkID, "provider2");
            return ErrorEnum::eNone;
        }));

    err = mNetworkManager->UpdateProviderNetwork(updatedProviders, nodeID);
    EXPECT_TRUE(err.IsNone());

    updatedProviders.Clear();

    EXPECT_CALL(*mStorage, RemoveHost(String("provider2"), nodeID)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, RemoveNetwork(String("provider2"))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mSender, SendNetwork(_, _))
        .WillOnce(Invoke([&](const std::string&, const std::vector<NetworkParameters>& params) -> Error {
            EXPECT_EQ(params.size(), 0);
            return ErrorEnum::eNone;
        }));

    err = mNetworkManager->UpdateProviderNetwork(updatedProviders, nodeID);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, UpdateProviderNetwork_StorageError)
{
    StaticArray<StaticString<cIDLen>, 1> providers;
    providers.PushBack("provider1");

    String nodeID = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetHosts(_, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mRandom, RandInt(_)).WillOnce(Return(RetWithError<uint64_t>(1000u, ErrorEnum::eNone)));

    EXPECT_CALL(*mStorage, AddNetwork(_)).WillOnce(Return(Error(ErrorEnum::eRuntime, "Storage error")));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->UpdateProviderNetwork(providers, nodeID);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, UpdateProviderNetwork_RandomError)
{
    StaticArray<StaticString<cIDLen>, 1> providers;
    providers.PushBack("provider1");

    String nodeID = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetHosts(_, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mRandom, RandInt(_))
        .WillOnce(Return(RetWithError<uint64_t>(0u, Error(ErrorEnum::eRuntime, "Random error"))));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->UpdateProviderNetwork(providers, nodeID);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, UpdateProviderNetwork_SenderError)
{
    StaticArray<StaticString<cIDLen>, 1> providers;
    providers.PushBack("provider1");

    String nodeID = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetHosts(_, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mRandom, RandInt(_)).WillOnce(Return(RetWithError<uint64_t>(1000u, ErrorEnum::eNone)));

    EXPECT_CALL(*mStorage, AddNetwork(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, AddHost(_, _)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mSender, SendNetwork(_, _)).WillOnce(Return(Error(ErrorEnum::eRuntime, "Sender error")));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->UpdateProviderNetwork(providers, nodeID);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, UpdateProviderNetwork_ExistingNetwork)
{
    StaticArray<StaticString<cIDLen>, 1> providers;
    providers.PushBack("existing_provider");

    String nodeID = "node1";

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Invoke([](Array<Network>& networks) -> Error {
        Network network;
        network.mNetworkID = "existing_provider";
        network.mSubnet    = "172.17.0.0/16";
        network.mVlanID    = 1000;
        networks.PushBack(network);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mStorage, GetHosts(String("existing_provider"), _))
        .WillOnce(Invoke([](const String&, Array<Host>& hosts) -> Error {
            Host host;
            host.mNodeID = "node1";
            host.mIP     = "172.17.0.1";
            hosts.PushBack(host);
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mStorage, GetInstances(String("existing_provider"), String("node1"), _))
        .WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mSender, SendNetwork(_, _))
        .WillOnce(Invoke([&](const std::string&, const std::vector<NetworkParameters>& params) -> Error {
            EXPECT_EQ(params.size(), 1);
            EXPECT_EQ(params[0].mNetworkID, "existing_provider");
            EXPECT_EQ(params[0].mSubnet, "172.17.0.0/16");
            EXPECT_EQ(params[0].mIP, "172.17.0.1");
            EXPECT_EQ(params[0].mVlanID, 1000);
            return ErrorEnum::eNone;
        }));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->UpdateProviderNetwork(providers, nodeID);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, PrepareInstanceNetworkParameters_NewInstance_Success)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network1";
    String nodeID    = "node1";

    cm::networkmanager::NetworkServiceData instanceData;
    NetworkParameters                      result1;
    NetworkParameters                      result2;

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

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent, networkID, nodeID, instanceData, result1);

    EXPECT_TRUE(err.IsNone());
    EXPECT_EQ(result1.mNetworkID, "network1");
    EXPECT_EQ(result1.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result1.mVlanID, 1000);
    EXPECT_EQ(result1.mDNSServers.Size(), 1);
    EXPECT_EQ(result1.mDNSServers[0], "8.8.8.8");
    EXPECT_FALSE(result1.mIP.IsEmpty());
    EXPECT_NE(result1.mIP, "172.17.0.2");

    InstanceIdent instanceIdent2;
    instanceIdent2.mItemID    = "service2";
    instanceIdent2.mSubjectID = "subject2";
    instanceIdent2.mInstance  = 1;

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent2, networkID, nodeID, instanceData, result2);
    EXPECT_TRUE(err.IsNone());

    EXPECT_NE(result1.mIP, result2.mIP);
    EXPECT_EQ(result1.mSubnet, result2.mSubnet);
    EXPECT_EQ(result1.mVlanID, result2.mVlanID);
    EXPECT_EQ(result1.mNetworkID, result2.mNetworkID);
    EXPECT_EQ(result2.mDNSServers.Size(), 1);
    EXPECT_EQ(result2.mDNSServers[0], "1.1.1.1");
}

TEST_F(CMNetworkManagerTest, PrepareInstanceNetworkParameters_ExistingInstance_Success)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network1";
    String nodeID    = "node1";

    cm::networkmanager::NetworkServiceData instanceData;
    NetworkParameters                      result;

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

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent, networkID, nodeID, instanceData, result);

    EXPECT_TRUE(err.IsNone());
    EXPECT_EQ(result.mNetworkID, "network1");
    EXPECT_EQ(result.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result.mVlanID, 1000);
    EXPECT_EQ(result.mIP, "172.17.0.10");
}

TEST_F(CMNetworkManagerTest, PrepareInstanceNetworkParameters_NetworkNotFound_Error)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network_nonexistent";
    String nodeID    = "node1";

    cm::networkmanager::NetworkServiceData instanceData;
    NetworkParameters                      result;

    EXPECT_CALL(*mStorage, GetNetworks(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetHosts(_, _)).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, GetInstances(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent, networkID, nodeID, instanceData, result);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, PrepareInstanceNetworkParameters_NodeNotFound_Error)
{
    InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 1;

    String networkID = "network1";
    String nodeID    = "node_nonexistent";

    cm::networkmanager::NetworkServiceData instanceData;
    NetworkParameters                      result;

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

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent, networkID, nodeID, instanceData, result);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, RemoveInstanceNetworkParameters_Success)
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

    EXPECT_CALL(*mStorage, RemoveInstance(instanceIdent)).WillOnce(Return(ErrorEnum::eNone));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->RemoveInstanceNetworkParameters(instanceIdent, nodeID);
    EXPECT_TRUE(err.IsNone());

    EXPECT_CALL(*mStorage, AddInstance(_)).WillOnce(Return(ErrorEnum::eNone));

    NetworkParameters                      result;
    cm::networkmanager::NetworkServiceData instanceData;

    EXPECT_CALL(*mDNSServer, GetIP()).WillOnce(Return("8.8.8.8"));

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent, "network1", nodeID, instanceData, result);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result.mVlanID, 1000);
    EXPECT_EQ(result.mNetworkID, "network1");
    EXPECT_FALSE(result.mIP.IsEmpty());
    EXPECT_EQ(result.mDNSServers.Size(), 1);
    EXPECT_EQ(result.mDNSServers[0], "8.8.8.8");
}

TEST_F(CMNetworkManagerTest, GetInstances_Success)
{
    InstanceIdent instance1;
    instance1.mItemID    = "service1";
    instance1.mSubjectID = "subject1";
    instance1.mInstance  = 1;

    InstanceIdent instance2;
    instance2.mItemID    = "service2";
    instance2.mSubjectID = "subject2";
    instance2.mInstance  = 2;

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
        .WillOnce(Invoke([&instance1, &instance2](const String&, const String&, Array<Instance>& instances) -> Error {
            Instance inst1;
            inst1.mNetworkID     = "network1";
            inst1.mNodeID        = "node1";
            inst1.mInstanceIdent = instance1;
            inst1.mIP            = "172.17.0.10";
            instances.PushBack(inst1);

            Instance inst2;
            inst2.mNetworkID     = "network1";
            inst2.mNodeID        = "node1";
            inst2.mInstanceIdent = instance2;
            inst2.mIP            = "172.17.0.11";
            instances.PushBack(inst2);

            return ErrorEnum::eNone;
        }));

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    StaticArray<InstanceIdent, 2> instances;
    err = mNetworkManager->GetInstances(instances);

    EXPECT_TRUE(err.IsNone());
    EXPECT_EQ(instances.Size(), 2);

    bool foundInstance1 = false;
    bool foundInstance2 = false;

    for (const auto& inst : instances) {
        if (inst.mItemID == instance1.mItemID && inst.mSubjectID == instance1.mSubjectID
            && inst.mInstance == instance1.mInstance) {
            foundInstance1 = true;
        } else if (inst.mItemID == instance2.mItemID && inst.mSubjectID == instance2.mSubjectID
            && inst.mInstance == instance2.mInstance) {
            foundInstance2 = true;
        }
    }

    EXPECT_TRUE(foundInstance1);
    EXPECT_TRUE(foundInstance2);
}

TEST_F(CMNetworkManagerTest, RestartDNSServer_Success)
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

    cm::networkmanager::NetworkServiceData instanceData1;
    instanceData1.mHosts.PushBack("custom1.example.com");
    instanceData1.mHosts.PushBack("api1.example.com");

    cm::networkmanager::NetworkServiceData instanceData2;
    instanceData2.mHosts.PushBack("custom2.example.com");

    NetworkParameters result1, result2;

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

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent1, networkID, nodeID, instanceData1, result1);
    EXPECT_TRUE(err.IsNone());

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent2, networkID, nodeID, instanceData2, result2);
    EXPECT_TRUE(err.IsNone());

    EXPECT_CALL(*mDNSServer, UpdateHostsFile(_)).WillOnce(Invoke([&](const HostsMap& hosts) -> Error {
        EXPECT_EQ(hosts.size(), 2);

        auto it1 = hosts.find(result1.mIP.CStr());
        auto it2 = hosts.find(result2.mIP.CStr());

        EXPECT_NE(it1, hosts.end());
        EXPECT_NE(it2, hosts.end());

        const auto& hosts1 = it1->second;
        EXPECT_TRUE(std::find(hosts1.begin(), hosts1.end(), "custom1.example.com") != hosts1.end());
        EXPECT_TRUE(std::find(hosts1.begin(), hosts1.end(), "api1.example.com") != hosts1.end());
        EXPECT_TRUE(std::find(hosts1.begin(), hosts1.end(), "1.subject1.service1") != hosts1.end());
        EXPECT_TRUE(std::find(hosts1.begin(), hosts1.end(), "1.subject1.service1.network1") != hosts1.end());

        const auto& hosts2 = it2->second;
        EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "custom2.example.com") != hosts2.end());
        EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "0.subject2.service2") != hosts2.end());
        EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "0.subject2.service2.network1") != hosts2.end());
        EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "subject2.service2") != hosts2.end());
        EXPECT_TRUE(std::find(hosts2.begin(), hosts2.end(), "subject2.service2.network1") != hosts2.end());

        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*mDNSServer, Restart()).WillOnce(Return(ErrorEnum::eNone));

    err = mNetworkManager->RestartDNSServer();
    EXPECT_TRUE(err.IsNone());
}

TEST_F(CMNetworkManagerTest, PrepareInstanceNetworkParameters_CrossNetworkFirewall_Success)
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

    cm::networkmanager::NetworkServiceData instanceData1;
    instanceData1.mExposedPorts.PushBack("8080/tcp");
    instanceData1.mExposedPorts.PushBack("9090/udp");

    cm::networkmanager::NetworkServiceData instanceData2;
    instanceData2.mAllowedConnections.PushBack("service1/8080/tcp");
    instanceData2.mAllowedConnections.PushBack("service1/9090/udp");

    NetworkParameters result1, result2;

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

    auto err = mNetworkManager->Init(*mStorage, *mRandom, *mSender, *mDNSServer);
    ASSERT_TRUE(err.IsNone());

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent1, networkID1, nodeID, instanceData1, result1);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result1.mNetworkID, "network1");
    EXPECT_EQ(result1.mSubnet, "172.17.0.0/16");
    EXPECT_EQ(result1.mVlanID, 1000);
    EXPECT_EQ(result1.mDNSServers.Size(), 1);
    EXPECT_EQ(result1.mDNSServers[0], "8.8.8.8");
    EXPECT_FALSE(result1.mIP.IsEmpty());
    EXPECT_NE(result1.mIP, "172.17.0.2");
    EXPECT_EQ(result1.mFirewallRules.Size(), 0);

    err = mNetworkManager->PrepareInstanceNetworkParameters(instanceIdent2, networkID2, nodeID, instanceData2, result2);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(result2.mNetworkID, "network2");
    EXPECT_EQ(result2.mSubnet, "172.18.0.0/16");
    EXPECT_EQ(result2.mVlanID, 2000);
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

} // namespace aos::cm::networkmanager::tests
