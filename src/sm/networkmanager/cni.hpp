/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_CNI_HPP_
#define AOS_SM_NETWORKMANAGER_CNI_HPP_

#include <string>
#include <unordered_map>
#include <vector>

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <core/sm/networkmanager/cni.hpp>

#include "exec.hpp"

namespace aos::sm::cni {
/**
 * CNI.
 */
class CNI : public CNIItf {
public:
    /**
     * Initializes CNI.
     *
     * @param exec Exec interface.
     * @return Error.
     */
    Error Init(ExecItf& exec);

    /**
     * Sets CNI configuration directory.
     *
     * @param cniConfigDir Path to CNI configuration directory.
     */
    Error SetConfDir(const String& configDir) override;

    /**
     * Executes a sequence of plugins with the ADD command
     *
     * @param net List of network configurations.
     * @param rt Runtime configuration parameters.
     * @param[out] result Result of the operation.
     * @return Error.
     */
    Error AddNetworkList(const NetworkConfigList& net, const RuntimeConf& rt, Result& result) override;

    /**
     * Executes a sequence of plugins with the DEL command
     *
     * @param net List of network configurations.
     * @param rt Runtime configuration parameters.
     * @return Error.
     */
    Error DeleteNetworkList(const NetworkConfigList& net, const RuntimeConf& rt) override;

    /**
     * Checks that a configuration is reasonably valid.
     *
     * @param net List of network configurations.
     * @return Error.
     */
    Error ValidateNetworkList(const NetworkConfigList& net) override;

    /**
     * Returns network list from cache.
     *
     * @param[out] net Network list.
     * @param[out] rt Runtime configuration.
     * @return Error.
     */
    Error GetNetworkListCachedConfig(NetworkConfigList& net, RuntimeConf& rt) override;

private:
    class ActionType {
    public:
        enum class Enum { eAdd, eDel, eCheck };

        static const Array<const char* const> GetStrings()
        {
            static const char* const sActionStrings[] = {"ADD", "DEL", "CHECK"};

            return Array<const char* const>(sActionStrings, ArraySize(sActionStrings));
        };
    };

    using ActionEnum = ActionType::Enum;
    using Action     = EnumStringer<ActionType>;

    static constexpr auto cBinaryPluginDir = "/opt/cni/bin";

    std::string ExecuteBridgePlugin(const NetworkConfigList& net, const std::string& prevResult,
        const std::string& args, std::vector<std::string>& plugins);
    std::string ExecuteFirewallPlugin(const NetworkConfigList& net, const std::string& prevResult,
        const std::string& args, std::vector<std::string>& plugins);
    std::string ExecuteBandwidthPlugin(const NetworkConfigList& net, const std::string& prevResult,
        const std::string& args, std::vector<std::string>& plugins);
    std::string ExecuteDNSPlugin(const NetworkConfigList& net, const RuntimeConf& rt, const std::string& prevResult,
        const std::string& args, std::vector<std::string>& plugins);
    std::string ArgsAsString(const RuntimeConf& rt, Action action) const;

    std::string CreateBridgePluginConfig(const BridgePluginConf& bridge) const;
    std::string BridgeConfigToJSON(
        const NetworkConfigList& net, const std::string& prevResult, std::vector<std::string>& plugins);

    void ParsePrevResult(const std::string& prevResult, Result& result) const;

    std::string CreateDNSPluginConfig(const DNSPluginConf& dns) const;
    std::string DNSConfigToJSON(const NetworkConfigList& net, const RuntimeConf& rt, const std::string& prevResult,
        std::vector<std::string>& plugins);
    std::string AddDNSRuntimeConfig(
        const std::string& pluginConfig, const std::string& name, const RuntimeConf& rt) const;

    std::string AddCNIData(const std::string& pluginConfig, const std::string& version, const std::string& name,
        const std::string& prevResult) const;

    std::string CreateFirewallPluginConfig(const FirewallPluginConf& firewall) const;
    std::string FirewallConfigToJSON(
        const NetworkConfigList& net, const std::string& prevResult, std::vector<std::string>& plugins);

    std::string CreateBandwidthPluginConfig(const BandwidthNetConf& bandwidth) const;
    std::string BandwidthConfigToJSON(
        const NetworkConfigList& net, const std::string& prevResult, std::vector<std::string>& plugins);

    std::string        CreatePluginsConfig(const NetworkConfigList& net, const std::vector<std::string>& plugins) const;
    Poco::JSON::Array  CreateCNIArgsArray(const RuntimeConf& rt) const;
    Poco::JSON::Object CreateCapabilityArgsObject(const RuntimeConf& rt, const std::string& networkName) const;
    std::string CreateCacheEntry(const NetworkConfigList& net, const RuntimeConf& rt, const std::string& prevResult,
        const std::vector<std::string>& plugins) const;

    void WriteCacheEntryToFile(const std::string& cacheEntry, const std::string& cachePath) const;

    std::string ResultToJSON(const Result& result) const;

    std::string mConfigDir;
    ExecItf*    mExec {};
};
} // namespace aos::sm::cni

#endif /* CNI_HPP_ */
