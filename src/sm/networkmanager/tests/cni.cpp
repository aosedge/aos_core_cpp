/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Parser.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <aos/test/log.hpp>
#include <utils/json.hpp>

#include "networkmanager/cni.hpp"

#include "mocks/cnimock.hpp"

using namespace aos;
using namespace aos::sm::cni;
using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CNITest : public ::Test {
protected:
    void SetUp() override
    {
        aos::test::InitLog();

        mExec = std::make_unique<::StrictMock<MockExec>>();
        mCNI.Init(*mExec.get());

        ASSERT_TRUE(std::filesystem::create_directories(mTestDir));

        auto err = mCNI.SetConfDir(mTestDir.c_str());
        ASSERT_TRUE(err.IsNone());
    }

    void TearDown() override { std::filesystem::remove_all("/tmp/cni_test_cache"); }

    void CompareJsonObjects(const std::string& actual, const std::string& expected)
    {
        auto [actualJson, err] = common::utils::ParseJson(actual);
        ASSERT_TRUE(err.IsNone());

        auto [expectedJson, err2] = common::utils::ParseJson(expected);
        ASSERT_TRUE(err2.IsNone());

        EXPECT_EQ(actualJson, expectedJson);
    }

    NetworkConfigList CreateTestBridgeNetworkConfig()
    {
        NetworkConfigList netConfig;
        netConfig.mVersion = "0.4.0";
        netConfig.mName    = "sp5f809a3c";

        netConfig.mBridge.mType        = "bridge";
        netConfig.mBridge.mBridge      = "br-sp5f809a3c";
        netConfig.mBridge.mIsGateway   = true;
        netConfig.mBridge.mIPMasq      = true;
        netConfig.mBridge.mHairpinMode = true;

        netConfig.mBridge.mIPAM.mType              = "host-local";
        netConfig.mBridge.mIPAM.mName              = "";
        netConfig.mBridge.mIPAM.mDataDir           = mTestDir.c_str();
        netConfig.mBridge.mIPAM.mRange.mSubnet     = "172.17.0.0/16";
        netConfig.mBridge.mIPAM.mRange.mRangeStart = "172.17.0.2";
        netConfig.mBridge.mIPAM.mRange.mRangeEnd   = "172.17.0.2";

        Router router;
        router.mDst = "0.0.0.0/0";
        netConfig.mBridge.mIPAM.mRouters.PushBack(router);

        return netConfig;
    }

    NetworkConfigList CreateTestDNSNetworkConfig()
    {
        NetworkConfigList netConfig = CreateTestBridgeNetworkConfig();

        netConfig.mDNS.mType                  = "dnsname";
        netConfig.mDNS.mMultiDomain           = true;
        netConfig.mDNS.mDomainName            = "sp5f809a3c";
        netConfig.mDNS.mCapabilities.mAliases = true;
        netConfig.mDNS.mRemoteServers.PushBack("10.0.0.100");

        return netConfig;
    }

    NetworkConfigList CreateTestFirewallNetworkConfig()
    {
        NetworkConfigList netConfig = CreateTestDNSNetworkConfig();

        netConfig.mFirewall.mType                   = "aos-firewall";
        netConfig.mFirewall.mUUID                   = "b8f745c9-9d8c-453e-8098-a29b0b4b9774";
        netConfig.mFirewall.mIptablesAdminChainName = "INSTANCE_b8f745c9-9d8c-453e-8098-a29b0b4b9774";
        netConfig.mFirewall.mAllowPublicConnections = true;

        InputAccessConfig inputRule;
        inputRule.mPort     = "80,443";
        inputRule.mProtocol = "tcp";
        netConfig.mFirewall.mInputAccess.PushBack(inputRule);

        OutputAccessConfig outputRule;
        outputRule.mDstIP   = "192.168.1.0/24";
        outputRule.mDstPort = "443";
        outputRule.mProto   = "tcp";
        outputRule.mSrcIP   = "172.17.0.2/16";
        netConfig.mFirewall.mOutputAccess.PushBack(outputRule);

        return netConfig;
    }

    RuntimeConf CreateTestRuntimeConfig()
    {
        RuntimeConf rtConfig;
        rtConfig.mContainerID = "b8f745c9-9d8c-453e-8098-a29b0b4b9774";
        rtConfig.mNetNS       = "/run/netns/testns";
        rtConfig.mIfName      = "eth0";

        Arg arg1;
        arg1.mName  = "IgnoreUnknown";
        arg1.mValue = "1";
        rtConfig.mArgs.PushBack(arg1);

        Arg arg2;
        arg2.mName  = "K8S_POD_NAME";
        arg2.mValue = "b8f745c9-9d8c-453e-8098-a29b0b4b9774";
        rtConfig.mArgs.PushBack(arg2);

        rtConfig.mCapabilityArgs.mHost.PushBack(
            "0.0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6");
        rtConfig.mCapabilityArgs.mHost.PushBack(
            "0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6");
        rtConfig.mCapabilityArgs.mHost.PushBack(
            "0.0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6.sp5f809a3c");
        rtConfig.mCapabilityArgs.mHost.PushBack(
            "0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6.sp5f809a3c");

        return rtConfig;
    }

    NetworkConfigList CreateTestBandwidthNetworkConfig()
    {
        NetworkConfigList netConfig = CreateTestFirewallNetworkConfig();

        netConfig.mBandwidth.mType         = "bandwidth";
        netConfig.mBandwidth.mIngressRate  = 126000;
        netConfig.mBandwidth.mIngressBurst = 12800;
        netConfig.mBandwidth.mEgressRate   = 126000;
        netConfig.mBandwidth.mEgressBurst  = 12800;

        return netConfig;
    }

    void ValidateResult(const Result& result)
    {
        EXPECT_STREQ(result.mVersion.CStr(), "0.4.0");

        ASSERT_EQ(result.mInterfaces.Size(), 3);

        EXPECT_STREQ(result.mInterfaces[0].mName.CStr(), "br-sp5f809a3c");
        EXPECT_STREQ(result.mInterfaces[0].mMac.CStr(), "aa:e8:7d:cb:b9:89");
        EXPECT_TRUE(result.mInterfaces[0].mSandbox.IsEmpty());

        EXPECT_STREQ(result.mInterfaces[1].mName.CStr(), "vethd5a0a2f5");
        EXPECT_STREQ(result.mInterfaces[1].mMac.CStr(), "56:98:87:e7:53:84");
        EXPECT_TRUE(result.mInterfaces[1].mSandbox.IsEmpty());

        EXPECT_STREQ(result.mInterfaces[2].mName.CStr(), "eth0");
        EXPECT_STREQ(result.mInterfaces[2].mMac.CStr(), "36:69:cc:de:ba:35");
        EXPECT_STREQ(result.mInterfaces[2].mSandbox.CStr(), "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774");

        ASSERT_EQ(result.mIPs.Size(), 1);
        EXPECT_STREQ(result.mIPs[0].mVersion.CStr(), "4");
        EXPECT_EQ(result.mIPs[0].mInterface, 2);
        EXPECT_STREQ(result.mIPs[0].mAddress.CStr(), "172.17.0.2/16");
        EXPECT_STREQ(result.mIPs[0].mGateway.CStr(), "172.17.0.1");

        ASSERT_EQ(result.mRoutes.Size(), 1);
        EXPECT_STREQ(result.mRoutes[0].mDst.CStr(), "0.0.0.0/0");

        ASSERT_EQ(result.mDNSServers.Size(), 1);
        EXPECT_STREQ(result.mDNSServers[0].CStr(), "172.17.0.1");
    }

    std::unique_ptr<MockExec> mExec;
    CNI                       mCNI;
    std::string               mTestDir = "/tmp/cni_test_cache";
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CNITest, TestAddNetworkList)
{
    const std::string expectedBridgeConfig = R"({
        "bridge": "br-sp5f809a3c",
        "cniVersion": "0.4.0",
        "hairpinMode": true,
        "ipMasq": true,
        "ipam": {
            "Name": "",
            "dataDir": "/tmp/cni_test_cache",
            "rangeEnd": "172.17.0.2",
            "rangeStart": "172.17.0.2",
            "routes": [
                {
                    "dst": "0.0.0.0/0"
                }
            ],
            "subnet": "172.17.0.0/16",
            "type": "host-local"
        },
        "isGateway": true,
        "name": "sp5f809a3c",
        "type": "bridge"
    })";

    const std::string expectedArgs = "CNI_COMMAND=ADD "
                                     "CNI_ARGS=IgnoreUnknown=1;K8S_POD_NAME=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_PATH=/opt/cni/bin "
                                     "CNI_CONTAINERID=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_NETNS=/run/netns/testns "
                                     "CNI_IFNAME=eth0";

    const std::string expectedPluginResult = R"({
        "cniVersion": "0.4.0",
        "dns": {
            "nameservers": [
                "172.17.0.1"
            ]
        },
        "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
        ],
        "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
        ],
        "routes": [
            {
                "dst": "0.0.0.0/0"
            }
        ]
    })";

    auto netConfig = CreateTestBridgeNetworkConfig();
    auto rtConfig  = CreateTestRuntimeConfig();

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bridge", _))
        .WillOnce(DoAll(WithArgs<0, 2>(Invoke([&](const std::string& config, const std::string& args) {
            CompareJsonObjects(config, expectedBridgeConfig);
            EXPECT_EQ(args, expectedArgs);
        })),
            Return(RetWithError<std::string> {expectedPluginResult, ErrorEnum::eNone})));

    Result result;
    auto   err = mCNI.AddNetworkList(netConfig, rtConfig, result);

    ASSERT_TRUE(err.IsNone());
    ValidateResult(result);
}

TEST_F(CNITest, TestExecuteDNSPlugin)
{
    const std::string expectedDNSConfig = R"({
        "capabilities": {
            "aliases": true
        },
        "cniVersion": "0.4.0",
        "domainName": "sp5f809a3c",
        "multiDomain": true,
        "name": "sp5f809a3c",
        "prevResult": {
            "cniVersion": "0.4.0",
            "dns": {
                "nameservers": ["172.17.0.1"]
            },
            "interfaces": [
                {
                    "mac": "aa:e8:7d:cb:b9:89",
                    "name": "br-sp5f809a3c"
                },
                {
                    "mac": "56:98:87:e7:53:84",
                    "name": "vethd5a0a2f5"
                },
                {
                    "mac": "36:69:cc:de:ba:35",
                    "name": "eth0",
                    "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
                }
            ],
            "ips": [
                {
                    "address": "172.17.0.2/16",
                    "gateway": "172.17.0.1",
                    "interface": 2,
                    "version": "4"
                }
            ],
            "routes": [
                {
                    "dst": "0.0.0.0/0"
                }
            ]
        },
        "remoteServers": ["10.0.0.100"],
        "runtimeConfig": {
            "aliases": {
                "sp5f809a3c": [
                    "0.0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6",
                    "0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6",
                    "0.0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6.sp5f809a3c",
                    "0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6.sp5f809a3c"
                ]
            }
        },
        "type": "dnsname"
    })";

    const std::string prevResult = R"({
        "cniVersion": "0.4.0",
        "dns": {
            "nameservers": ["172.17.0.1"]
        },
        "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
        ],
        "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
        ],
        "routes": [
            {
                "dst": "0.0.0.0/0"
            }
        ]
    })";

    auto netConfig = CreateTestDNSNetworkConfig();
    auto rtConfig  = CreateTestRuntimeConfig();

    const std::string expectedArgs = "CNI_COMMAND=ADD "
                                     "CNI_ARGS=IgnoreUnknown=1;K8S_POD_NAME=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_PATH=/opt/cni/bin "
                                     "CNI_CONTAINERID=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_NETNS=/run/netns/testns "
                                     "CNI_IFNAME=eth0";

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bridge", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/dnsname", _))
        .WillOnce(DoAll(WithArgs<0, 2>(Invoke([&](const std::string& config, const std::string& args) {
            CompareJsonObjects(config, expectedDNSConfig);
            EXPECT_EQ(args, expectedArgs);
        })),
            Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone})));

    Result result;

    auto err = mCNI.AddNetworkList(netConfig, rtConfig, result);

    ASSERT_TRUE(err.IsNone());
    ValidateResult(result);
}

TEST_F(CNITest, TestExecuteFirewallPlugin)
{
    const std::string expectedFirewallConfig = R"({
        "allowPublicConnections": true,
        "cniVersion": "0.4.0",
        "inputAccess": [
            {
                "port": "80,443",
                "protocol": "tcp"
            }
        ],
        "outputAccess": [
            {
                "dstIp": "192.168.1.0/24",
                "dstPort": "443",
                "proto": "tcp",
                "srcIp": "172.17.0.2/16"
            }
        ],
        "iptablesAdminChainName": "INSTANCE_b8f745c9-9d8c-453e-8098-a29b0b4b9774",
        "name": "sp5f809a3c",
        "uuid": "b8f745c9-9d8c-453e-8098-a29b0b4b9774",
        "type": "aos-firewall",
        "prevResult": {
            "cniVersion": "0.4.0",
            "dns": {
                "nameservers": ["172.17.0.1"]
            },
            "interfaces": [
                {
                    "mac": "aa:e8:7d:cb:b9:89",
                    "name": "br-sp5f809a3c"
                },
                {
                    "mac": "56:98:87:e7:53:84",
                    "name": "vethd5a0a2f5"
                },
                {
                    "mac": "36:69:cc:de:ba:35",
                    "name": "eth0",
                    "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
                }
            ],
            "ips": [
                {
                    "address": "172.17.0.2/16",
                    "gateway": "172.17.0.1",
                    "interface": 2,
                    "version": "4"
                }
            ],
            "routes": [
                {
                    "dst": "0.0.0.0/0"
                }
            ]
        }
    })";

    auto netConfig = CreateTestFirewallNetworkConfig();
    auto rtConfig  = CreateTestRuntimeConfig();

    const std::string prevResult = R"({
        "cniVersion": "0.4.0",
        "dns": {
            "nameservers": ["172.17.0.1"]
        },
        "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
        ],
        "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
        ],
        "routes": [
            {
                "dst": "0.0.0.0/0"
            }
        ]
    })";

    const std::string expectedArgs = "CNI_COMMAND=ADD "
                                     "CNI_ARGS=IgnoreUnknown=1;K8S_POD_NAME=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_PATH=/opt/cni/bin "
                                     "CNI_CONTAINERID=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_NETNS=/run/netns/testns "
                                     "CNI_IFNAME=eth0";

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bridge", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/aos-firewall", _))
        .WillOnce(DoAll(WithArgs<0, 2>(Invoke([&](const std::string& config, const std::string& args) {
            CompareJsonObjects(config, expectedFirewallConfig);
            EXPECT_EQ(args, expectedArgs);
        })),
            Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone})));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/dnsname", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    Result result;

    auto err = mCNI.AddNetworkList(netConfig, rtConfig, result);

    ASSERT_TRUE(err.IsNone());
    ValidateResult(result);
}

TEST_F(CNITest, TestExecuteBandwidthPlugin)
{
    const std::string expectedBandwidthConfig = R"({
        "cniVersion": "0.4.0",
        "egressBurst": 12800,
        "egressRate": 126000,
        "ingressBurst": 12800,
        "ingressRate": 126000,
        "name": "sp5f809a3c",
        "prevResult": {
            "cniVersion": "0.4.0",
            "dns": {
                "nameservers": ["172.17.0.1"]
            },
            "interfaces": [
                {
                    "mac": "aa:e8:7d:cb:b9:89",
                    "name": "br-sp5f809a3c"
                },
                {
                    "mac": "56:98:87:e7:53:84",
                    "name": "vethd5a0a2f5"
                },
                {
                    "mac": "36:69:cc:de:ba:35",
                    "name": "eth0",
                    "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
                }
            ],
            "ips": [
                {
                    "address": "172.17.0.2/16",
                    "gateway": "172.17.0.1",
                    "interface": 2,
                    "version": "4"
                }
            ],
            "routes": [
                {
                    "dst": "0.0.0.0/0"
                }
            ]
        },
        "type": "bandwidth"
    })";

    auto netConfig = CreateTestBandwidthNetworkConfig();
    auto rtConfig  = CreateTestRuntimeConfig();

    const std::string prevResult = R"({
        "cniVersion": "0.4.0",
        "dns": {
            "nameservers": ["172.17.0.1"]
        },
        "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
        ],
        "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
        ],
        "routes": [
            {
                "dst": "0.0.0.0/0"
            }
        ]
    })";

    const std::string expectedArgs = "CNI_COMMAND=ADD "
                                     "CNI_ARGS=IgnoreUnknown=1;K8S_POD_NAME=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_PATH=/opt/cni/bin "
                                     "CNI_CONTAINERID=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_NETNS=/run/netns/testns "
                                     "CNI_IFNAME=eth0";

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bridge", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/aos-firewall", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bandwidth", _))
        .WillOnce(DoAll(WithArgs<0, 2>(Invoke([&](const std::string& config, const std::string& args) {
            CompareJsonObjects(config, expectedBandwidthConfig);
            EXPECT_EQ(args, expectedArgs);
        })),
            Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone})));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/dnsname", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    Result result;

    auto err = mCNI.AddNetworkList(netConfig, rtConfig, result);

    ASSERT_TRUE(err.IsNone());
    ValidateResult(result);
}

TEST_F(CNITest, TestCacheFileContent)
{

    const std::string expectedCache = R"({
    "capabilityArgs": {
        "aliases": {
            "sp5f809a3c": [
                "0.0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6",
                "0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6",
                "0.0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6.sp5f809a3c",
                "0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6.sp5f809a3c"
            ]
        }
    },
    "cniArgs": [
        [
            "IgnoreUnknown",
            "1"
        ],
        [
            "K8S_POD_NAME",
            "b8f745c9-9d8c-453e-8098-a29b0b4b9774"
        ]
    ],
    "config": )"
                                      R"("eyJjbmlWZXJzaW9uIjoiMC40LjAiLCJuYW1lIjoic3A1ZjgwOWEzYyIsInBsdWdpbnMiOlt7\r\n)"
                                      R"(ImJyaWRnZSI6ImJyLXNwNWY4MDlhM2MiLCJoYWlycGluTW9kZSI6dHJ1ZSwiaXBNYXNxIjp0\r\n)"
                                      R"(cnVlLCJpcGFtIjp7Ik5hbWUiOiIiLCJkYXRhRGlyIjoiL3RtcC9jbmlfdGVzdF9jYWNoZSIs\r\n)"
                                      R"(InJhbmdlRW5kIjoiMTcyLjE3LjAuMiIsInJhbmdlU3RhcnQiOiIxNzIuMTcuMC4yIiwicm91\r\n)"
                                      R"(dGVzIjpbeyJkc3QiOiIwLjAuMC4wLzAifV0sInN1Ym5ldCI6IjE3Mi4xNy4wLjAvMTYiLCJ0\r\n)"
                                      R"(eXBlIjoiaG9zdC1sb2NhbCJ9LCJpc0dhdGV3YXkiOnRydWUsInR5cGUiOiJicmlkZ2UifSx7\r\n)"
                                      R"(ImNhcGFiaWxpdGllcyI6eyJhbGlhc2VzIjp0cnVlfSwiZG9tYWluTmFtZSI6InNwNWY4MDlh\r\n)"
                                      R"(M2MiLCJtdWx0aURvbWFpbiI6dHJ1ZSwicmVtb3RlU2VydmVycyI6WyIxMC4wLjAuMTAwIl0s\r\n)"
                                      R"(InR5cGUiOiJkbnNuYW1lIn0seyJhbGxvd1B1YmxpY0Nvbm5lY3Rpb25zIjp0cnVlLCJpbnB1\r\n)"
                                      R"(dEFjY2VzcyI6W3sicG9ydCI6IjgwLDQ0MyIsInByb3RvY29sIjoidGNwIn1dLCJpcHRhYmxl\r\n)"
                                      R"(c0FkbWluQ2hhaW5OYW1lIjoiSU5TVEFOQ0VfYjhmNzQ1YzktOWQ4Yy00NTNlLTgwOTgtYTI5\r\n)"
                                      R"(YjBiNGI5Nzc0Iiwib3V0cHV0QWNjZXNzIjpbeyJkc3RJcCI6IjE5Mi4xNjguMS4wLzI0Iiwi\r\n)"
                                      R"(ZHN0UG9ydCI6IjQ0MyIsInByb3RvIjoidGNwIiwic3JjSXAiOiIxNzIuMTcuMC4yLzE2In1d\r\n)"
                                      R"(LCJ0eXBlIjoiYW9zLWZpcmV3YWxsIiwidXVpZCI6ImI4Zjc0NWM5LTlkOGMtNDUzZS04MDk4\r\n)"
                                      R"(LWEyOWIwYjRiOTc3NCJ9LHsiZWdyZXNzQnVyc3QiOjEyODAwLCJlZ3Jlc3NSYXRlIjoxMjYw\r\n)"
                                      R"(MDAsImluZ3Jlc3NCdXJzdCI6MTI4MDAsImluZ3Jlc3NSYXRlIjoxMjYwMDAsInR5cGUiOiJi\r\n)"
                                      R"(YW5kd2lkdGgifV19")"
                                      R"(,
    "containerId": "b8f745c9-9d8c-453e-8098-a29b0b4b9774",
    "ifName": "eth0",
    "kind": "cniCacheV1",
    "networkName": "sp5f809a3c",
    "result": {
        "cniVersion": "0.4.0",
        "dns": {
            "nameservers": [
                "172.17.0.1"
            ]
        },
        "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
        ],
        "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
        ],
        "routes": [
            {
                "dst": "0.0.0.0/0"
            }
        ]
    }
    })";

    const std::string prevResult = R"({
        "cniVersion": "0.4.0",
        "dns": {
            "nameservers": ["172.17.0.1"]
        },
        "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
        ],
        "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
        ],
        "routes": [
            {
                "dst": "0.0.0.0/0"
            }
        ]
    })";

    auto netConfig = CreateTestBandwidthNetworkConfig();
    auto rtConfig  = CreateTestRuntimeConfig();

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bridge", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/aos-firewall", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bandwidth", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/dnsname", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    Result result;

    auto err = mCNI.AddNetworkList(netConfig, rtConfig, result);

    ASSERT_TRUE(err.IsNone());
    ValidateResult(result);

    std::string cacheFilePath = mTestDir + "/results/" + netConfig.mName.CStr() + "-" + rtConfig.mContainerID.CStr();
    ASSERT_TRUE(std::filesystem::exists(cacheFilePath));

    std::ifstream cacheFile(cacheFilePath);
    ASSERT_TRUE(cacheFile.is_open());

    std::string cacheContent((std::istreambuf_iterator<char>(cacheFile)), std::istreambuf_iterator<char>());

    CompareJsonObjects(cacheContent, expectedCache);
}

TEST_F(CNITest, TestGetNetworkListCachedConfig)
{
    const std::string prevResult = R"({
        "cniVersion": "0.4.0",
        "dns": {
            "nameservers": ["172.17.0.1"]
        },
        "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
        ],
        "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
        ],
        "routes": [
            {
                "dst": "0.0.0.0/0"
            }
        ]
    })";

    auto netConfig = CreateTestBandwidthNetworkConfig();
    auto rtConfig  = CreateTestRuntimeConfig();

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bridge", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/aos-firewall", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bandwidth", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/dnsname", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    Result result;

    auto err = mCNI.AddNetworkList(netConfig, rtConfig, result);
    ASSERT_TRUE(err.IsNone());

    NetworkConfigList readNetConfig;
    readNetConfig.mName = netConfig.mName;

    RuntimeConf readRtConfig;
    readRtConfig.mContainerID = rtConfig.mContainerID;

    err = mCNI.GetNetworkListCachedConfig(readNetConfig, readRtConfig);
    ASSERT_TRUE(err.IsNone());

    EXPECT_STREQ(readNetConfig.mName.CStr(), netConfig.mName.CStr());

    EXPECT_STREQ(readNetConfig.mBridge.mType.CStr(), netConfig.mBridge.mType.CStr());
    EXPECT_STREQ(readNetConfig.mBridge.mBridge.CStr(), netConfig.mBridge.mBridge.CStr());
    EXPECT_EQ(readNetConfig.mBridge.mIsGateway, netConfig.mBridge.mIsGateway);
    EXPECT_EQ(readNetConfig.mBridge.mIPMasq, netConfig.mBridge.mIPMasq);
    EXPECT_EQ(readNetConfig.mBridge.mHairpinMode, netConfig.mBridge.mHairpinMode);
    EXPECT_STREQ(readNetConfig.mBridge.mIPAM.mType.CStr(), netConfig.mBridge.mIPAM.mType.CStr());
    EXPECT_STREQ(readNetConfig.mBridge.mIPAM.mDataDir.CStr(), netConfig.mBridge.mIPAM.mDataDir.CStr());
    EXPECT_STREQ(readNetConfig.mBridge.mIPAM.mRange.mSubnet.CStr(), netConfig.mBridge.mIPAM.mRange.mSubnet.CStr());
    EXPECT_STREQ(
        readNetConfig.mBridge.mIPAM.mRange.mRangeStart.CStr(), netConfig.mBridge.mIPAM.mRange.mRangeStart.CStr());
    EXPECT_STREQ(readNetConfig.mBridge.mIPAM.mRange.mRangeEnd.CStr(), netConfig.mBridge.mIPAM.mRange.mRangeEnd.CStr());

    EXPECT_STREQ(readNetConfig.mDNS.mType.CStr(), netConfig.mDNS.mType.CStr());
    EXPECT_EQ(readNetConfig.mDNS.mMultiDomain, netConfig.mDNS.mMultiDomain);
    EXPECT_STREQ(readNetConfig.mDNS.mDomainName.CStr(), netConfig.mDNS.mDomainName.CStr());
    EXPECT_EQ(readNetConfig.mDNS.mCapabilities.mAliases, netConfig.mDNS.mCapabilities.mAliases);

    EXPECT_STREQ(readNetConfig.mFirewall.mType.CStr(), netConfig.mFirewall.mType.CStr());
    EXPECT_STREQ(readNetConfig.mFirewall.mUUID.CStr(), netConfig.mFirewall.mUUID.CStr());
    EXPECT_STREQ(
        readNetConfig.mFirewall.mIptablesAdminChainName.CStr(), netConfig.mFirewall.mIptablesAdminChainName.CStr());
    EXPECT_EQ(readNetConfig.mFirewall.mAllowPublicConnections, netConfig.mFirewall.mAllowPublicConnections);

    EXPECT_STREQ(readNetConfig.mBandwidth.mType.CStr(), netConfig.mBandwidth.mType.CStr());
    EXPECT_EQ(readNetConfig.mBandwidth.mIngressRate, netConfig.mBandwidth.mIngressRate);
    EXPECT_EQ(readNetConfig.mBandwidth.mIngressBurst, netConfig.mBandwidth.mIngressBurst);
    EXPECT_EQ(readNetConfig.mBandwidth.mEgressRate, netConfig.mBandwidth.mEgressRate);
    EXPECT_EQ(readNetConfig.mBandwidth.mEgressBurst, netConfig.mBandwidth.mEgressBurst);

    EXPECT_STREQ(readRtConfig.mIfName.CStr(), rtConfig.mIfName.CStr());
    EXPECT_EQ(readRtConfig.mArgs.Size(), rtConfig.mArgs.Size());
    for (size_t i = 0; i < rtConfig.mArgs.Size(); ++i) {
        EXPECT_STREQ(readRtConfig.mArgs[i].mName.CStr(), rtConfig.mArgs[i].mName.CStr());
        EXPECT_STREQ(readRtConfig.mArgs[i].mValue.CStr(), rtConfig.mArgs[i].mValue.CStr());
    }

    EXPECT_EQ(readRtConfig.mCapabilityArgs.mHost.Size(), rtConfig.mCapabilityArgs.mHost.Size());
    for (size_t i = 0; i < rtConfig.mCapabilityArgs.mHost.Size(); ++i) {
        EXPECT_STREQ(readRtConfig.mCapabilityArgs.mHost[i].CStr(), rtConfig.mCapabilityArgs.mHost[i].CStr());
    }

    ValidateResult(readNetConfig.mPrevResult);
}

TEST_F(CNITest, TestNetworkListLifecycle)
{
    const std::string prevResult = R"({
        "cniVersion": "0.4.0",
        "dns": {
            "nameservers": ["172.17.0.1"]
        },
        "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
        ],
        "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
        ],
        "routes": [
            {
                "dst": "0.0.0.0/0"
            }
        ]
    })";

    auto netConfig = CreateTestBandwidthNetworkConfig();
    auto rtConfig  = CreateTestRuntimeConfig();

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bridge", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/aos-firewall", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bandwidth", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/dnsname", _))
        .WillOnce(Return(RetWithError<std::string> {prevResult, ErrorEnum::eNone}));

    Result result;

    auto addErr = mCNI.AddNetworkList(netConfig, rtConfig, result);
    ASSERT_TRUE(addErr.IsNone());

    std::string cacheFilePath = mTestDir + "/results/" + netConfig.mName.CStr() + "-" + rtConfig.mContainerID.CStr();
    ASSERT_TRUE(std::filesystem::exists(cacheFilePath));

    NetworkConfigList readNetConfig;
    readNetConfig.mName    = netConfig.mName;
    readNetConfig.mVersion = netConfig.mVersion;
    RuntimeConf readRtConfig;
    readRtConfig.mContainerID = rtConfig.mContainerID;
    readRtConfig.mNetNS       = rtConfig.mNetNS;

    auto readErr = mCNI.GetNetworkListCachedConfig(readNetConfig, readRtConfig);
    ASSERT_TRUE(readErr.IsNone());

    const std::string expectedBridgeConfig = R"({
        "bridge": "br-sp5f809a3c",
        "cniVersion": "0.4.0",
        "hairpinMode": true,
        "ipMasq": true,
        "ipam": {
            "Name": "",
            "dataDir": "/tmp/cni_test_cache",
            "rangeEnd": "172.17.0.2",
            "rangeStart": "172.17.0.2",
            "routes": [
            {
                "dst": "0.0.0.0/0"
            }
            ],
            "subnet": "172.17.0.0/16",
            "type": "host-local"
        },
        "isGateway": true,
        "name": "sp5f809a3c",
        "prevResult": {
            "cniVersion": "0.4.0",
            "dns": {
            "nameservers": [
                "172.17.0.1"
            ]
            },
            "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
            ],
            "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
            ],
            "routes": [
            {
                "dst": "0.0.0.0/0"
            }
            ]
        },
        "type": "bridge"
    })";

    const std::string expectedFirewallConfig = R"({
        "allowPublicConnections": true,
        "cniVersion": "0.4.0",
        "inputAccess": [
            {
            "port": "80,443",
            "protocol": "tcp"
            }
        ],
        "iptablesAdminChainName": "INSTANCE_b8f745c9-9d8c-453e-8098-a29b0b4b9774",
        "name": "sp5f809a3c",
        "outputAccess": [
            {
            "dstIp": "192.168.1.0/24",
            "dstPort": "443",
            "proto": "tcp",
            "srcIp": "172.17.0.2/16"
            }
        ],
        "prevResult": {
            "cniVersion": "0.4.0",
            "dns": {
            "nameservers": [
                "172.17.0.1"
            ]
            },
            "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
            ],
            "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
            ],
            "routes": [
            {
                "dst": "0.0.0.0/0"
            }
            ]
        },
        "type": "aos-firewall",
        "uuid": "b8f745c9-9d8c-453e-8098-a29b0b4b9774"
    })";

    const std::string expectedBandwidthConfig = R"({
        "cniVersion": "0.4.0",
        "egressBurst": 12800,
        "egressRate": 126000,
        "ingressBurst": 12800,
        "ingressRate": 126000,
        "name": "sp5f809a3c",
        "prevResult": {
            "cniVersion": "0.4.0",
            "dns": {
            "nameservers": [
                "172.17.0.1"
            ]
            },
            "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
            ],
            "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
            ],
            "routes": [
            {
                "dst": "0.0.0.0/0"
            }
            ]
        },
        "type": "bandwidth"
    })";

    const std::string expectedDNSConfig = R"({
        "capabilities": {
            "aliases": true
        },
        "cniVersion": "0.4.0",
        "domainName": "sp5f809a3c",
        "multiDomain": true,
        "name": "sp5f809a3c",
        "prevResult": {
            "cniVersion": "0.4.0",
            "dns": {
            "nameservers": [
                "172.17.0.1"
            ]
            },
            "interfaces": [
            {
                "mac": "aa:e8:7d:cb:b9:89",
                "name": "br-sp5f809a3c"
            },
            {
                "mac": "56:98:87:e7:53:84",
                "name": "vethd5a0a2f5"
            },
            {
                "mac": "36:69:cc:de:ba:35",
                "name": "eth0",
                "sandbox": "/run/netns/b8f745c9-9d8c-453e-8098-a29b0b4b9774"
            }
            ],
            "ips": [
            {
                "address": "172.17.0.2/16",
                "gateway": "172.17.0.1",
                "interface": 2,
                "version": "4"
            }
            ],
            "routes": [
            {
                "dst": "0.0.0.0/0"
            }
            ]
        },
        "remoteServers": [
            "10.0.0.100"
        ],
        "runtimeConfig": {
            "aliases": {
            "sp5f809a3c": [
                "0.0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6",
                "0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6",
                "0.0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6.sp5f809a3c",
                "0faa910e-7db0-4219-bd89-f4b3491c4b9f.ccd33206-d81e-482e-beb9-8f3b1af5aac6.sp5f809a3c"
            ]
            }
        },
        "type": "dnsname"
    })";

    const std::string expectedArgs = "CNI_COMMAND=DEL "
                                     "CNI_ARGS=IgnoreUnknown=1;K8S_POD_NAME=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_PATH=/opt/cni/bin "
                                     "CNI_CONTAINERID=b8f745c9-9d8c-453e-8098-a29b0b4b9774 "
                                     "CNI_NETNS=/run/netns/testns "
                                     "CNI_IFNAME=eth0";

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bridge", _))
        .WillOnce(DoAll(WithArgs<0, 2>(Invoke([&](const std::string& config, const std::string& args) {
            CompareJsonObjects(config, expectedBridgeConfig);

            EXPECT_EQ(args, expectedArgs);
        })),
            Return(RetWithError<std::string> {"", ErrorEnum::eNone})));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/aos-firewall", _))
        .WillOnce(DoAll(WithArgs<0, 2>(Invoke([&](const std::string& config, const std::string& args) {
            CompareJsonObjects(config, expectedFirewallConfig);

            EXPECT_EQ(args, expectedArgs);
        })),
            Return(RetWithError<std::string> {"", ErrorEnum::eNone})));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/bandwidth", _))
        .WillOnce(DoAll(WithArgs<0, 2>(Invoke([&](const std::string& config, const std::string& args) {
            CompareJsonObjects(config, expectedBandwidthConfig);

            EXPECT_EQ(args, expectedArgs);
        })),
            Return(RetWithError<std::string> {"", ErrorEnum::eNone})));

    EXPECT_CALL(*mExec, ExecPlugin(_, "/opt/cni/bin/dnsname", _))
        .WillOnce(DoAll(WithArgs<0, 2>(Invoke([&](const std::string& config, const std::string& args) {
            CompareJsonObjects(config, expectedDNSConfig);

            EXPECT_EQ(args, expectedArgs);
        })),
            Return(RetWithError<std::string> {"", ErrorEnum::eNone})));

    auto deleteErr = mCNI.DeleteNetworkList(readNetConfig, readRtConfig);
    ASSERT_TRUE(deleteErr.IsNone());

    EXPECT_FALSE(std::filesystem::exists(cacheFilePath));
}
