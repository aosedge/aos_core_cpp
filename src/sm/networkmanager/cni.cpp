/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <Poco/Base64Decoder.h>
#include <Poco/Base64Encoder.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <sm/logger/logmodule.hpp>

#include "cni.hpp"

namespace aos::sm::cni {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

template <typename InputContainer, typename OutputContainer>
void Copy(const InputContainer& input, OutputContainer& output)
{
    for (const auto& item : input) {
        auto err = output.PushBack(item);
        AOS_ERROR_CHECK_AND_THROW(err, "can't copy container item");
    }
}

template <typename OutputContainer>
void Copy(const std::vector<std::string>& input, OutputContainer& output)
{
    for (const auto& item : input) {
        auto err = output.PushBack(item.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't copy container item");
    }
}

Interface InterfaceFromJson(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return {
        object.GetOptionalValue<std::string>("name").value_or("").c_str(),
        object.GetOptionalValue<std::string>("mac").value_or("").c_str(),
        object.GetOptionalValue<std::string>("sandbox").value_or("").c_str(),
    };
}

IPs IPsFromJson(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return {object.GetOptionalValue<std::string>("version").value_or("").c_str(),
        object.GetOptionalValue<int>("interface").value_or(0),
        object.GetOptionalValue<std::string>("address").value_or("").c_str(),
        object.GetOptionalValue<std::string>("gateway").value_or("").c_str()};
}

Router RouterFromJson(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return {
        object.GetOptionalValue<std::string>("dst").value_or("").c_str(),
        object.GetOptionalValue<std::string>("gw").value_or("").c_str(),
    };
}

InputAccessConfig InputAccessConfigFromJson(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return {
        object.GetOptionalValue<std::string>("port").value_or("").c_str(),
        object.GetOptionalValue<std::string>("protocol").value_or("").c_str(),
    };
}

OutputAccessConfig OutputAccessConfigFromJson(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return {
        object.GetOptionalValue<std::string>("dstIp").value_or("").c_str(),
        object.GetOptionalValue<std::string>("dstPort").value_or("").c_str(),
        object.GetOptionalValue<std::string>("proto").value_or("").c_str(),
        object.GetOptionalValue<std::string>("srcIp").value_or("").c_str(),
    };
}

void ParseBridgeConfig(const aos::common::utils::CaseInsensitiveObjectWrapper& plugin, BridgePluginConf& bridge)
{
    bridge.mType        = plugin.GetValue<std::string>("type").c_str();
    bridge.mBridge      = plugin.GetOptionalValue<std::string>("bridge").value_or("").c_str();
    bridge.mIsGateway   = plugin.GetOptionalValue<bool>("isGateway").value_or(false);
    bridge.mIPMasq      = plugin.GetOptionalValue<bool>("ipMasq").value_or(false);
    bridge.mHairpinMode = plugin.GetOptionalValue<bool>("hairpinMode").value_or(false);

    auto ipam = plugin.GetObject("ipam");

    bridge.mIPAM.mType    = ipam.GetOptionalValue<std::string>("type").value_or("").c_str();
    bridge.mIPAM.mName    = ipam.GetOptionalValue<std::string>("Name").value_or("").c_str();
    bridge.mIPAM.mDataDir = ipam.GetOptionalValue<std::string>("dataDir").value_or("").c_str();

    bridge.mIPAM.mRange.mSubnet     = ipam.GetOptionalValue<std::string>("subnet").value_or("").c_str();
    bridge.mIPAM.mRange.mRangeStart = ipam.GetOptionalValue<std::string>("rangeStart").value_or("").c_str();
    bridge.mIPAM.mRange.mRangeEnd   = ipam.GetOptionalValue<std::string>("rangeEnd").value_or("").c_str();

    auto routes = aos::common::utils::GetArrayValue<Router>(ipam, "routes",
        [](const auto& value) { return RouterFromJson(aos::common::utils::CaseInsensitiveObjectWrapper(value)); });

    Copy(routes, bridge.mIPAM.mRouters);
}

void ParseDNSConfig(const aos::common::utils::CaseInsensitiveObjectWrapper& plugin, DNSPluginConf& dns)
{
    dns.mType        = plugin.GetValue<std::string>("type").c_str();
    dns.mMultiDomain = plugin.GetOptionalValue<bool>("multiDomain").value_or(false);
    dns.mDomainName  = plugin.GetOptionalValue<std::string>("domainName").value_or("").c_str();

    auto capabilities = plugin.GetObject("capabilities");

    dns.mCapabilities.mAliases = capabilities.GetOptionalValue<bool>("aliases").value_or(false);

    auto remoteServers = aos::common::utils::GetArrayValue<std::string>(plugin, "remoteServers");

    Copy(remoteServers, dns.mRemoteServers);
}

void ParseFirewallConfig(const aos::common::utils::CaseInsensitiveObjectWrapper& plugin, FirewallPluginConf& firewall)
{
    firewall.mType = plugin.GetValue<std::string>("type").c_str();
    firewall.mUUID = plugin.GetOptionalValue<std::string>("uuid").value_or("").c_str();
    firewall.mIptablesAdminChainName
        = plugin.GetOptionalValue<std::string>("iptablesAdminChainName").value_or("").c_str();
    firewall.mAllowPublicConnections = plugin.GetOptionalValue<bool>("allowPublicConnections").value_or(false);

    auto inputAccess
        = aos::common::utils::GetArrayValue<InputAccessConfig>(plugin, "inputAccess", [](const auto& value) {
              return InputAccessConfigFromJson(aos::common::utils::CaseInsensitiveObjectWrapper(value));
          });

    Copy(inputAccess, firewall.mInputAccess);

    auto outputAccess
        = aos::common::utils::GetArrayValue<OutputAccessConfig>(plugin, "outputAccess", [](const auto& value) {
              return OutputAccessConfigFromJson(aos::common::utils::CaseInsensitiveObjectWrapper(value));
          });

    Copy(outputAccess, firewall.mOutputAccess);
}

void ParseBandwidthConfig(const aos::common::utils::CaseInsensitiveObjectWrapper& plugin, BandwidthNetConf& bandwidth)
{
    bandwidth.mType         = plugin.GetValue<std::string>("type").c_str();
    bandwidth.mIngressRate  = plugin.GetOptionalValue<uint64_t>("ingressRate").value_or(0);
    bandwidth.mIngressBurst = plugin.GetOptionalValue<uint64_t>("ingressBurst").value_or(0);
    bandwidth.mEgressRate   = plugin.GetOptionalValue<uint64_t>("egressRate").value_or(0);
    bandwidth.mEgressBurst  = plugin.GetOptionalValue<uint64_t>("egressBurst").value_or(0);
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CNI::Init(ExecItf& exec)
{
    LOG_DBG() << "Init CNI";

    mExec = &exec;

    return ErrorEnum::eNone;
}

Error CNI::SetConfDir(const String& configDir)
{
    LOG_DBG() << "Set CNI configuration directory: configDir=" << configDir.CStr();

    mConfigDir = configDir.CStr();

    return ErrorEnum::eNone;
}

Error CNI::AddNetworkList(const NetworkConfigList& net, const RuntimeConf& rt, Result& result)
{
    LOG_DBG() << "Add network list: name=" << net.mName.CStr();

    try {
        auto prevResult = ResultToJSON(net.mPrevResult);
        auto args       = ArgsAsString(rt, ActionEnum::eAdd);

        std::vector<std::string> plugins;

        prevResult = ExecuteBridgePlugin(net, prevResult, args, plugins);
        prevResult = ExecuteDNSPlugin(net, rt, prevResult, args, plugins);
        prevResult = ExecuteFirewallPlugin(net, prevResult, args, plugins);
        prevResult = ExecuteBandwidthPlugin(net, prevResult, args, plugins);

        ParsePrevResult(prevResult, result);
        auto path = std::filesystem::path(mConfigDir) / (net.mName.CStr() + std::string("-") + rt.mContainerID.CStr());

        WriteCacheEntryToFile(CreateCacheEntry(net, rt, prevResult, plugins), path);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error CNI::DeleteNetworkList(const NetworkConfigList& net, const RuntimeConf& rt)
{
    LOG_DBG() << "Delete network list: name=" << net.mName.CStr();

    try {
        auto prevResult = ResultToJSON(net.mPrevResult);
        auto args       = ArgsAsString(rt, ActionEnum::eDel);

        std::vector<std::string> plugins;

        ExecuteBridgePlugin(net, prevResult, args, plugins);
        ExecuteDNSPlugin(net, rt, prevResult, args, plugins);
        ExecuteFirewallPlugin(net, prevResult, args, plugins);
        ExecuteBandwidthPlugin(net, prevResult, args, plugins);

        if (!std::filesystem::remove(
                std::filesystem::path(mConfigDir) / (net.mName.CStr() + std::string("-") + rt.mContainerID.CStr()))) {
            return Error(ErrorEnum::eFailed, "failed to remove cache file");
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error CNI::ValidateNetworkList(const NetworkConfigList& net)
{
    (void)net;
    return ErrorEnum::eNone;
}

Error CNI::GetNetworkListCachedConfig(NetworkConfigList& net, RuntimeConf& rt)
{
    try {
        auto cacheFilePath
            = std::filesystem::path(mConfigDir) / (net.mName.CStr() + std::string("-") + rt.mContainerID.CStr());
        if (!std::filesystem::exists(cacheFilePath)) {
            return Error(ErrorEnum::eFailed, "cache file not found");
        }

        std::ifstream cacheFile(cacheFilePath);
        if (!cacheFile.is_open()) {
            return Error(ErrorEnum::eFailed, "failed to open cache file");
        }

        std::string cacheContent((std::istreambuf_iterator<char>(cacheFile)), std::istreambuf_iterator<char>());
        cacheFile.close();

        auto [cacheJson, err] = common::utils::ParseJson(cacheContent);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        common::utils::CaseInsensitiveObjectWrapper cacheObj(cacheJson);

        auto kind = cacheObj.GetValue<std::string>("kind");
        if (kind != "cniCacheV1") {
            return Error(ErrorEnum::eFailed, "cache file has invalid kind field");
        }

        if (!cacheObj.Has("config")) {
            return Error(ErrorEnum::eFailed, "cache file does not contain config field");
        }

        std::string         encodedConfig = cacheObj.GetValue<std::string>("config");
        std::istringstream  encodedStream(encodedConfig);
        Poco::Base64Decoder decoder(encodedStream);
        std::string         decodedConfig;

        std::copy(std::istreambuf_iterator<char>(decoder), std::istreambuf_iterator<char>(),
            std::back_inserter(decodedConfig));

        auto [configJson, configErr] = common::utils::ParseJson(decodedConfig);
        if (!configErr.IsNone()) {
            return AOS_ERROR_WRAP(configErr);
        }

        common::utils::CaseInsensitiveObjectWrapper pluginJson(configJson);

        auto plugins = pluginJson.GetArray("plugins");

        for (size_t i = 0; i < plugins->size(); ++i) {
            auto plugin = common::utils::CaseInsensitiveObjectWrapper(plugins->getObject(i));

            auto pluginType = plugin.GetValue<std::string>("type");
            if (pluginType == "bridge") {
                ParseBridgeConfig(plugin, net.mBridge);
            } else if (pluginType == "dnsname") {
                ParseDNSConfig(plugin, net.mDNS);
            } else if (pluginType == "aos-firewall") {
                ParseFirewallConfig(plugin, net.mFirewall);
            } else if (pluginType == "bandwidth") {
                ParseBandwidthConfig(plugin, net.mBandwidth);
            }
        }

        if (cacheObj.Has("cniArgs")) {
            const auto args = aos::common::utils::GetArrayValue<Arg>(cacheObj, "cniArgs", [](const auto& value) {
                auto argPair = value.template extract<Poco::JSON::Array::Ptr>();

                return Arg {
                    argPair->get(0).toString().c_str(),
                    argPair->get(1).toString().c_str(),
                };
            });

            Copy(args, rt.mArgs);
        }

        if (cacheObj.Has("capabilityArgs")) {
            auto capabilityArgs = cacheObj.GetObject("capabilityArgs");
            if (capabilityArgs.Has("aliases")) {
                Copy(aos::common::utils::GetArrayValue<std::string>(
                         capabilityArgs.GetObject("aliases"), net.mName.CStr()),
                    rt.mCapabilityArgs.mHost);
            }
        }

        rt.mIfName = cacheObj.GetOptionalValue<std::string>("ifName").value_or("").c_str();

        ParsePrevResult(cacheObj.GetValue<std::string>("result"), net.mPrevResult);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void CNI::WriteCacheEntryToFile(const std::string& cacheEntry, const std::string& cachePath) const
{
    std::ofstream cacheFile(cachePath);
    if (!cacheFile.is_open()) {
        throw std::runtime_error("failed to open cache file");
    }

    cacheFile << cacheEntry;
}

std::string CNI::ResultToJSON(const Result& result) const
{
    if (result.mVersion.IsEmpty()) {
        return std::string {};
    }

    Poco::JSON::Object jsonRoot;

    jsonRoot.set("cniVersion", result.mVersion.CStr());

    if (!result.mDNSServers.IsEmpty()) {
        Poco::JSON::Object dnsObj;
        Poco::JSON::Array  nameserversArray;

        for (const auto& server : result.mDNSServers) {
            if (!server.IsEmpty()) {
                nameserversArray.add(server.CStr());
            }
        }

        dnsObj.set("nameservers", nameserversArray);

        jsonRoot.set("dns", dnsObj);
    }

    Poco::JSON::Array interfacesArray;

    for (const auto& iface : result.mInterfaces) {
        if (!iface.mName.IsEmpty()) {
            Poco::JSON::Object ifaceObj;

            ifaceObj.set("name", iface.mName.CStr());
            ifaceObj.set("mac", iface.mMac.CStr());

            if (!iface.mSandbox.IsEmpty()) {
                ifaceObj.set("sandbox", iface.mSandbox.CStr());
            }

            interfacesArray.add(ifaceObj);
        }
    }

    jsonRoot.set("interfaces", interfacesArray);

    Poco::JSON::Array ipsArray;

    for (const auto& ip : result.mIPs) {
        if (!ip.mAddress.IsEmpty()) {
            Poco::JSON::Object ipObj;

            ipObj.set("version", ip.mVersion.CStr());
            ipObj.set("interface", ip.mInterface);
            ipObj.set("address", ip.mAddress.CStr());
            ipObj.set("gateway", ip.mGateway.CStr());

            ipsArray.add(ipObj);
        }
    }

    jsonRoot.set("ips", ipsArray);

    Poco::JSON::Array routesArray;

    for (const auto& route : result.mRoutes) {
        if (!route.mDst.IsEmpty()) {
            Poco::JSON::Object routeObj;

            routeObj.set("dst", route.mDst.CStr());

            if (!route.mGW.IsEmpty()) {
                routeObj.set("gw", route.mGW.CStr());
            }

            routesArray.add(routeObj);
        }
    }

    jsonRoot.set("routes", routesArray);

    std::ostringstream oss;

    jsonRoot.stringify(oss);

    return oss.str();
}

std::string CNI::ExecuteBridgePlugin(const NetworkConfigList& net, const std::string& prevResult,
    const std::string& args, std::vector<std::string>& plugins)
{
    if (net.mBridge.mType.IsEmpty()) {
        return std::string {};
    }

    LOG_DBG() << "Execute bridge plugin: name=" << net.mName.CStr();

    auto bridgeConfig = BridgeConfigToJSON(net, prevResult, plugins);
    auto pluginPath   = std::filesystem::path(cBinaryPluginDir) / net.mBridge.mType.CStr();

    auto [result, err] = mExec->ExecPlugin(bridgeConfig, pluginPath, args);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to execute bridge plugin");

    return result;
}

std::string CNI::ExecuteDNSPlugin(const NetworkConfigList& net, const RuntimeConf& rt, const std::string& prevResult,
    const std::string& args, std::vector<std::string>& plugins)
{
    if (net.mDNS.mType.IsEmpty()) {
        return prevResult;
    }

    LOG_DBG() << "Execute DNS plugin: name=" << net.mName.CStr();

    auto dnsConfig  = DNSConfigToJSON(net, rt, prevResult, plugins);
    auto pluginPath = std::filesystem::path(cBinaryPluginDir) / net.mDNS.mType.CStr();

    auto [result, err] = mExec->ExecPlugin(dnsConfig, pluginPath, args);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to execute DNS plugin");

    return result;
}

std::string CNI::ExecuteFirewallPlugin(const NetworkConfigList& net, const std::string& prevResult,
    const std::string& args, std::vector<std::string>& plugins)
{
    if (net.mFirewall.mType.IsEmpty()) {
        return prevResult;
    }

    LOG_DBG() << "Execute firewall plugin: name=" << net.mName.CStr();

    auto firewallConfig = FirewallConfigToJSON(net, prevResult, plugins);
    auto pluginPath     = std::filesystem::path(cBinaryPluginDir) / net.mFirewall.mType.CStr();

    auto [result, err] = mExec->ExecPlugin(firewallConfig, pluginPath, args);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to execute firewall plugin");

    return result;
}

std::string CNI::CreateBridgePluginConfig(const BridgePluginConf& bridge) const
{
    Poco::JSON::Object jsonRoot;

    jsonRoot.set("type", bridge.mType.CStr());
    jsonRoot.set("bridge", bridge.mBridge.CStr());
    jsonRoot.set("isGateway", bridge.mIsGateway);
    jsonRoot.set("ipMasq", bridge.mIPMasq);
    jsonRoot.set("hairpinMode", bridge.mHairpinMode);

    Poco::JSON::Object ipamObj;

    ipamObj.set("type", bridge.mIPAM.mType.CStr());
    ipamObj.set("Name", bridge.mIPAM.mName.CStr());
    ipamObj.set("dataDir", bridge.mIPAM.mDataDir.CStr());

    const auto& range = bridge.mIPAM.mRange;
    if (!range.mSubnet.IsEmpty()) {
        ipamObj.set("subnet", range.mSubnet.CStr());
    }

    if (!range.mRangeStart.IsEmpty()) {
        ipamObj.set("rangeStart", range.mRangeStart.CStr());
    }

    if (!range.mRangeEnd.IsEmpty()) {
        ipamObj.set("rangeEnd", range.mRangeEnd.CStr());
    }

    if (!range.mGateway.IsEmpty()) {
        ipamObj.set("gateway", range.mGateway.CStr());
    }

    Poco::JSON::Array routesArray;

    for (const auto& router : bridge.mIPAM.mRouters) {
        if (!router.mDst.IsEmpty()) {
            Poco::JSON::Object routeObj;
            routeObj.set("dst", router.mDst.CStr());

            if (!router.mGW.IsEmpty()) {
                routeObj.set("gw", router.mGW.CStr());
            }

            routesArray.add(routeObj);
        }
    }

    if (!routesArray.empty()) {
        ipamObj.set("routes", routesArray);
    }

    jsonRoot.set("ipam", ipamObj);

    std::ostringstream oss;

    jsonRoot.stringify(oss);

    return oss.str();
}

std::string CNI::BridgeConfigToJSON(
    const NetworkConfigList& net, const std::string& prevResult, std::vector<std::string>& plugins)
{
    auto pluginConfig = CreateBridgePluginConfig(net.mBridge);

    plugins.push_back(pluginConfig);

    return AddCNIData(pluginConfig, net.mVersion.CStr(), net.mName.CStr(), prevResult);
}

std::string CNI::CreateFirewallPluginConfig(const FirewallPluginConf& firewall) const
{
    Poco::JSON::Object jsonRoot;

    jsonRoot.set("type", firewall.mType.CStr());
    jsonRoot.set("uuid", firewall.mUUID.CStr());
    jsonRoot.set("iptablesAdminChainName", firewall.mIptablesAdminChainName.CStr());
    jsonRoot.set("allowPublicConnections", firewall.mAllowPublicConnections);

    Poco::JSON::Array inputAccessArray;

    for (const auto& input : firewall.mInputAccess) {
        if (!input.mPort.IsEmpty()) {
            Poco::JSON::Object inputRule;
            inputRule.set("port", input.mPort.CStr());

            if (!input.mProtocol.IsEmpty()) {
                inputRule.set("protocol", input.mProtocol.CStr());
            }

            inputAccessArray.add(inputRule);
        }
    }

    if (!inputAccessArray.empty()) {
        jsonRoot.set("inputAccess", inputAccessArray);
    }

    Poco::JSON::Array outputAccessArray;

    for (const auto& output : firewall.mOutputAccess) {
        Poco::JSON::Object outputRule;

        if (!output.mDstIP.IsEmpty()) {
            outputRule.set("dstIp", output.mDstIP.CStr());
        }

        if (!output.mDstPort.IsEmpty()) {
            outputRule.set("dstPort", output.mDstPort.CStr());
        }

        if (!output.mProto.IsEmpty()) {
            outputRule.set("proto", output.mProto.CStr());
        }

        if (!output.mSrcIP.IsEmpty()) {
            outputRule.set("srcIp", output.mSrcIP.CStr());
        }

        outputAccessArray.add(outputRule);
    }

    if (!outputAccessArray.empty()) {
        jsonRoot.set("outputAccess", outputAccessArray);
    }

    std::ostringstream oss;

    jsonRoot.stringify(oss);

    return oss.str();
}

std::string CNI::FirewallConfigToJSON(
    const NetworkConfigList& net, const std::string& prevResult, std::vector<std::string>& plugins)
{
    auto pluginConfig = CreateFirewallPluginConfig(net.mFirewall);

    plugins.push_back(pluginConfig);

    return AddCNIData(pluginConfig, net.mVersion.CStr(), net.mName.CStr(), prevResult);
}

std::string CNI::CreateBandwidthPluginConfig(const BandwidthNetConf& bandwidth) const
{
    Poco::JSON::Object jsonRoot;

    jsonRoot.set("type", bandwidth.mType.CStr());
    jsonRoot.set("ingressRate", bandwidth.mIngressRate);
    jsonRoot.set("ingressBurst", bandwidth.mIngressBurst);
    jsonRoot.set("egressRate", bandwidth.mEgressRate);
    jsonRoot.set("egressBurst", bandwidth.mEgressBurst);

    std::ostringstream oss;

    jsonRoot.stringify(oss);

    return oss.str();
}

std::string CNI::BandwidthConfigToJSON(
    const NetworkConfigList& net, const std::string& prevResult, std::vector<std::string>& plugins)
{
    auto pluginConfig = CreateBandwidthPluginConfig(net.mBandwidth);

    plugins.push_back(pluginConfig);

    return AddCNIData(pluginConfig, net.mVersion.CStr(), net.mName.CStr(), prevResult);
}

std::string CNI::ExecuteBandwidthPlugin(const NetworkConfigList& net, const std::string& prevResult,
    const std::string& args, std::vector<std::string>& plugins)
{
    if (net.mBandwidth.mType.IsEmpty()) {
        return prevResult;
    }

    auto bandwidthConfig = BandwidthConfigToJSON(net, prevResult, plugins);
    auto pluginPath      = std::string(cBinaryPluginDir) + "/" + net.mBandwidth.mType.CStr();

    auto [result, err] = mExec->ExecPlugin(bandwidthConfig, pluginPath, args);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to execute bandwidth plugin");

    return result;
}

std::string CNI::CreateDNSPluginConfig(const DNSPluginConf& dns) const
{
    Poco::JSON::Object jsonRoot;

    jsonRoot.set("type", dns.mType.CStr());
    jsonRoot.set("multiDomain", dns.mMultiDomain);
    jsonRoot.set("domainName", dns.mDomainName.CStr());

    Poco::JSON::Object capabilitiesObj;

    capabilitiesObj.set("aliases", dns.mCapabilities.mAliases);
    jsonRoot.set("capabilities", capabilitiesObj);

    Poco::JSON::Array remoteServersArray;

    for (const auto& server : dns.mRemoteServers) {
        if (!server.IsEmpty()) {
            remoteServersArray.add(server.CStr());
        }
    }

    jsonRoot.set("remoteServers", remoteServersArray);

    std::ostringstream oss;

    jsonRoot.stringify(oss);

    return oss.str();
}

std::string CNI::AddDNSRuntimeConfig(
    const std::string& pluginConfig, const std::string& name, const RuntimeConf& rt) const
{
    if (rt.mCapabilityArgs.mHost.IsEmpty()) {
        return pluginConfig;
    }

    auto [json, err] = common::utils::ParseJson(pluginConfig);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse plugin config");

    auto jsonRoot = json.extract<Poco::JSON::Object::Ptr>();

    Poco::JSON::Object runtimeConfig;
    Poco::JSON::Object aliases;
    Poco::JSON::Array  aliasesArray;

    for (const auto& host : rt.mCapabilityArgs.mHost) {
        if (!host.IsEmpty()) {
            aliasesArray.add(host.CStr());
        }
    }

    if (!aliasesArray.empty()) {
        aliases.set(name, aliasesArray);
        runtimeConfig.set("aliases", aliases);
        jsonRoot->set("runtimeConfig", runtimeConfig);
    }

    std::ostringstream oss;

    jsonRoot->stringify(oss);

    return oss.str();
}

std::string CNI::AddCNIData(const std::string& pluginConfig, const std::string& version, const std::string& name,
    const std::string& prevResult) const
{
    auto [json, err] = common::utils::ParseJson(pluginConfig);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse plugin config");

    auto jsonRoot = json.extract<Poco::JSON::Object::Ptr>();

    jsonRoot->set("cniVersion", version);
    jsonRoot->set("name", name);

    if (!prevResult.empty()) {
        Tie(json, err) = common::utils::ParseJson(prevResult);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse previous result");

        jsonRoot->set("prevResult", json.extract<Poco::JSON::Object::Ptr>());
    }

    std::ostringstream oss;

    jsonRoot->stringify(oss);

    return oss.str();
}

std::string CNI::DNSConfigToJSON(const NetworkConfigList& net, const RuntimeConf& rt, const std::string& prevResult,
    std::vector<std::string>& plugins)
{
    auto pluginConfig = CreateDNSPluginConfig(net.mDNS);

    plugins.push_back(pluginConfig);

    auto configWithRuntime = AddDNSRuntimeConfig(pluginConfig, net.mName.CStr(), rt);
    return AddCNIData(configWithRuntime, net.mVersion.CStr(), net.mName.CStr(), prevResult);
}

std::string CNI::ArgsAsString(const RuntimeConf& rt, Action action) const
{
    LOG_DBG() << "Create args string: action=" << action;

    std::ostringstream argsStream;
    for (const auto& arg : rt.mArgs) {
        if (!arg.mName.IsEmpty() && !arg.mValue.IsEmpty()) {
            if (!argsStream.str().empty()) {
                argsStream << ";";
            }
            argsStream << arg.mName.CStr() << "=" << arg.mValue.CStr();
        }
    }

    std::string argsStr = argsStream.str();

    std::vector<std::string> envs = {"CNI_COMMAND=" + std::string(action.ToString().CStr()), "CNI_ARGS=" + argsStr,
        "CNI_PATH=" + std::string(cBinaryPluginDir), "CNI_CONTAINERID=" + std::string(rt.mContainerID.CStr())};

    if (!rt.mNetNS.IsEmpty()) {
        envs.push_back("CNI_NETNS=" + std::string(rt.mNetNS.CStr()));
    }

    if (!rt.mIfName.IsEmpty()) {
        envs.push_back("CNI_IFNAME=" + std::string(rt.mIfName.CStr()));
    }

    return std::accumulate(envs.begin(), envs.end(), std::string {},
        [](const std::string& acc, const std::string& env) { return acc.empty() ? env : acc + " " + env; });
}

void CNI::ParsePrevResult(const std::string& prevResult, Result& result) const
{
    if (prevResult.empty()) {
        return;
    }

    auto [json, err] = common::utils::ParseJson(prevResult);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to parse previous result");

    common::utils::CaseInsensitiveObjectWrapper object(json);

    result.mVersion = object.GetValue<std::string>("cniVersion").c_str();

    const auto interfaces = aos::common::utils::GetArrayValue<Interface>(object, "interfaces",
        [](const auto& value) { return InterfaceFromJson(aos::common::utils::CaseInsensitiveObjectWrapper(value)); });

    Copy(interfaces, result.mInterfaces);

    const auto ips = aos::common::utils::GetArrayValue<IPs>(object, "ips",
        [](const auto& value) { return IPsFromJson(aos::common::utils::CaseInsensitiveObjectWrapper(value)); });

    Copy(ips, result.mIPs);

    const auto routers = aos::common::utils::GetArrayValue<Router>(object, "routes",
        [](const auto& value) { return RouterFromJson(aos::common::utils::CaseInsensitiveObjectWrapper(value)); });

    Copy(routers, result.mRoutes);

    if (object.Has("dns")) {
        const auto dns = aos::common::utils::GetArrayValue<std::string>(object.GetObject("dns"), "nameservers");

        Copy(dns, result.mDNSServers);
    }
}

std::string CNI::CreatePluginsConfig(const NetworkConfigList& net, const std::vector<std::string>& plugins) const
{
    Poco::JSON::Object jsonRoot;

    jsonRoot.set("name", net.mName.CStr());
    jsonRoot.set("cniVersion", net.mVersion.CStr());

    Poco::JSON::Array pluginsArray;

    for (const auto& pluginConfig : plugins) {
        if (!pluginConfig.empty()) {
            auto [json, err] = common::utils::ParseJson(pluginConfig);
            AOS_ERROR_CHECK_AND_THROW(err, "failed to parse plugin config");

            auto jsonObject = json.extract<Poco::JSON::Object::Ptr>();

            pluginsArray.add(jsonObject);
        }
    }

    jsonRoot.set("plugins", pluginsArray);

    std::ostringstream oss;

    jsonRoot.stringify(oss);

    return oss.str();
}

Poco::JSON::Array CNI::CreateCNIArgsArray(const RuntimeConf& rt) const
{
    Poco::JSON::Array argsArray;

    for (const auto& arg : rt.mArgs) {
        if (!arg.mName.IsEmpty() && !arg.mValue.IsEmpty()) {
            Poco::JSON::Array pairArray;

            pairArray.add(arg.mName.CStr());
            pairArray.add(arg.mValue.CStr());
            argsArray.add(pairArray);
        }
    }

    return argsArray;
}

Poco::JSON::Object CNI::CreateCapabilityArgsObject(const RuntimeConf& rt, const std::string& networkName) const
{
    Poco::JSON::Object capabilityArgs;

    if (!rt.mCapabilityArgs.mHost.IsEmpty()) {
        Poco::JSON::Object aliases;
        Poco::JSON::Array  aliasesArray;

        for (const auto& host : rt.mCapabilityArgs.mHost) {
            if (!host.IsEmpty()) {
                aliasesArray.add(host.CStr());
            }
        }

        if (!aliasesArray.empty()) {
            aliases.set(networkName, aliasesArray);
            capabilityArgs.set("aliases", aliases);
        }
    }

    return capabilityArgs;
}

std::string CNI::CreateCacheEntry(const NetworkConfigList& net, const RuntimeConf& rt, const std::string& prevResult,
    const std::vector<std::string>& plugins) const
{
    Poco::JSON::Object cacheEntry;

    cacheEntry.set("kind", "cniCacheV1");
    cacheEntry.set("containerId", rt.mContainerID.CStr());
    cacheEntry.set("ifName", rt.mIfName.CStr());
    cacheEntry.set("networkName", net.mName.CStr());

    std::string         configStr = CreatePluginsConfig(net, plugins);
    std::ostringstream  encodedStream;
    Poco::Base64Encoder encoder(encodedStream);
    encoder << configStr;
    encoder.close();

    cacheEntry.set("config", encodedStream.str());
    cacheEntry.set("cniArgs", CreateCNIArgsArray(rt));
    cacheEntry.set("capabilityArgs", CreateCapabilityArgsObject(rt, net.mName.CStr()));

    if (!prevResult.empty()) {
        auto [json, err] = common::utils::ParseJson(prevResult);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse previous result");

        cacheEntry.set("result", json.extract<Poco::JSON::Object::Ptr>());
    }

    std::ostringstream oss;

    cacheEntry.stringify(oss);

    return oss.str();
}

} // namespace aos::sm::cni
