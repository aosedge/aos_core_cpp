/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_SMCONTROLLER_HPP_
#define AOS_CM_SMCONTROLLER_SMCONTROLLER_HPP_

#include <core/cm/smcontroller/itf/instancestatusreceiver.hpp>
#include <core/cm/smcontroller/itf/sminforeceiver.hpp>
#include <core/common/monitoring/monitoring.hpp>
#include <core/common/tools/error.hpp>
#include <core/common/types/alerts.hpp>
#include <core/common/types/common.hpp>
#include <core/common/types/envvars.hpp>
#include <core/common/types/instance.hpp>
#include <core/common/types/log.hpp>
#include <core/common/types/unitconfig.hpp>

namespace aos::cm::smcontroller {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/**
 * Node config status.
 */
struct NodeConfigStatus {
    UnitConfigState           mState;
    StaticString<cVersionLen> mVersion;
    Error                     mError;

    /**
     * Compares node config status.
     *
     * @param rhs node config status to compare with.
     * @return bool.
     */
    bool operator==(const NodeConfigStatus& rhs) const
    {
        return mState == rhs.mState && mVersion == rhs.mVersion && mError == rhs.mError;
    }

    /**
     * Compares node config status.
     *
     * @param rhs node config status to compare with.
     * @return bool.
     */
    bool operator!=(const NodeConfigStatus& rhs) const { return !operator==(rhs); }
};

/**
 * Cloud connection status enum.
 */
class CloudConnectionStatusType {
public:
    enum class Enum {
        eDisconnected,
        eConnected,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sStrings[] = {
            "disconnected",
            "connected",
        };

        return Array<const char* const>(sStrings, ArraySize(sStrings));
    };
};

using CloudConnectionStatusEnum = CloudConnectionStatusType::Enum;
using CloudConnectionStatus     = EnumStringer<CloudConnectionStatusType>;

static constexpr auto cMaxNumEnvVars = 10;

/**
 * Override env vars.
 */
struct OverrideEnvVars {
    StaticArray<InstanceFilter, cMaxNumInstances> mInstances;
    StaticArray<EnvVarInfo, cMaxNumEnvVars>       mEnvVars;
};

/***********************************************************************************************************************
 * Interfaces
 **********************************************************************************************************************/

/**
 * Log data listener interface.
 */
class LogDataListenerItf {
public:
    /**
     * Destructor.
     */
    virtual ~LogDataListenerItf() = default;

    /**
     * Called when log data is available.
     *
     * @param log log data.
     * @return Error.
     */
    virtual Error OnLogData(const PushLog& log) = 0;
};

/**
 * Instant monitoring data listener interface.
 */
class InstantMonitoringListenerItf {
public:
    /**
     * Destructor.
     */
    virtual ~InstantMonitoringListenerItf() = default;

    /**
     * Notifies about instant monitoring data for node.
     *
     * @param monitoring node monitoring data.
     * @return Error.
     */
    virtual Error OnInstantMonitoring(const monitoring::NodeMonitoringData& monitoring) = 0;
};

/**
 * Alert listener interface.
 */
class AlertListenerItf {
public:
    /**
     * Destructor.
     */
    virtual ~AlertListenerItf() = default;

    /**
     * Notifies about alert.
     *
     * @param nodeID node ID.
     * @param alert alert.
     * @return Error.
     */
    virtual Error OnAlert(const String& nodeID, const AlertVariant& alert) = 0;
};

/**
 * SM controller interface.
 */
class SMControllerItf : public InstanceStatusReceiverItf, public SMInfoReceiverItf {
public:
    /**
     * Destructor.
     */
    virtual ~SMControllerItf() = default;

    //
    // Node config API
    //

    /**
     * Returns node config status.
     *
     * @param nodeID node ID.
     * @param[out] status node config status.
     * @return Error.
     */
    virtual Error GetNodeConfigStatus(const String& nodeID, NodeConfigStatus& status) = 0;

    /**
     * Checks node config.
     *
     * @param nodeID node ID.
     * @param config node config.
     * @return Error.
     */
    virtual Error CheckNodeConfig(const String& nodeID, const NodeConfig& config) = 0;

    /**
     * Sets node config.
     *
     * @param nodeID node ID.
     * @param config node config.
     * @return Error.
     */
    virtual Error SetNodeConfig(const String& nodeID, const NodeConfig& config) = 0;

    //
    // Run instance API
    //

    /**
     * Updates running instances.
     *
     * @param startInstances running instances to start.
     * @param stopInstances running instances to stop.
     * @return Error.
     */
    virtual Error UpdateInstances(
        const String& nodeID, const Array<InstanceInfo>& startInstances, const Array<InstanceIdent>& stopInstances)
        = 0;

    //
    // Env vars API
    //

    /**
     * Overrides instance's environment variables.
     *
     * @param overrideInstanceEnvVar override instance environment variables.
     * @return Error.
     */
    virtual Error OverrideEnvVars(const OverrideEnvVars& overrideEnvVars) = 0;

    //
    // Log API
    //

    /**
     * Requests system log.
     *
     * @param request log request.
     * @return Error.
     */
    virtual Error GetLog(const RequestLog& request) = 0;

    /**
     * Subscribes log data listener.
     *
     * @param listener log data listener.
     * @return Error.
     */
    virtual Error SubscribeLogDataListener(LogDataListenerItf& listener) = 0;

    /**
     * Unsubscribes log data listener.
     *
     * @param listener log data listener.
     * @return Error.
     */
    virtual Error UnsubscribeLogDataListener(LogDataListenerItf& listener) = 0;

    //
    // Monitoring API
    //

    /**
     * Returns average node monitoring data.
     *
     * @param nodeID node ID.
     * @param[out] monitoring average node monitoring data.
     * @return Error.
     */
    virtual Error GetAverageNodeMonitoring(const String& nodeID, monitoring::MonitoringData& monitoring) const = 0;

    /**
     * Subscribes instant monitoring listener.
     *
     * @param listener instant monitoring listener.
     * @return Error.
     */
    virtual Error SubscribeInstantMonitoringListener(InstantMonitoringListenerItf& listener) = 0;

    /**
     * Unsubscribes instant monitoring listener.
     *
     * @param listener instant monitoring listener.
     * @return Error.
     */
    virtual Error UnsubscribeInstantMonitoringListener(InstantMonitoringListenerItf& listener) = 0;

    //
    // Network API
    //

    /**
     * Sets connection status.
     * TOOD: clarify whether SM requires cloud connection status(if not - remove from proto as well)
     *
     * @param status connection status.
     * @return Error.
     */
    virtual Error SetConnectionStatus(CloudConnectionStatus status) = 0;

    /**
     * Updates networks.
     *
     * @param nodeID node ID.
     * @param networks networks to update.
     * @return Error.
     */
    virtual Error UpdateNetworks(const String& nodeID, const Array<NetworkParameters>& networks) = 0;

    //
    // Clock API
    //

    /**
     * Synchronizes clock.
     *
     * @param nodeID node ID.
     * @return Error.
     */
    virtual Error SyncClock(const String& nodeID) = 0;

    //
    // Alerts API
    //

    /**
     * Subscribes alert listener.
     *
     * @param listener alert listener.
     * @return Error.
     */
    virtual Error SubscribeAlertListener(AlertListenerItf& listener) = 0;

    /**
     * Unsubscribes alert listener.
     *
     * @param listener alert listener.
     * @return Error.
     */
    virtual Error UnsubscribeAlertListener(AlertListenerItf& listener) = 0;
};

} // namespace aos::cm::smcontroller

#endif
