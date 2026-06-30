/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <cstdio>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>

#include "bridgenetwork.hpp"

namespace aos::sm::networkmanager {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error BridgeNetwork::Init(InterfaceManagerItf& netIf)
{
    mNetIf = &netIf;

    return ErrorEnum::eNone;
}

Error BridgeNetwork::Attach(const String& instanceID, const BridgeParams& params, BridgeAttachResult& result)
{
    LOG_DBG() << "Attach bridge" << Log::Field("instanceID", instanceID) << Log::Field("bridge", params.mBridgeIfName);

    const auto hostName = VethName(instanceID, "veth");

    Error err;

    // Create the veth pair with the peer placed directly into the instance netns
    // already named as the container interface, and the host side already
    // enslaved to the bridge and up. One netlink op replaces the former
    // create + move + rename + setmaster + setup sequence; the per-instance
    // netns means the container name cannot collide with another instance.
    if (err = mNetIf->CreateVethToNamespace(hostName, params.mContainerIfName, params.mNetNSPath, params.mBridgeIfName);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto cleanupVeth = DeferRelease(&hostName, [this, &err](const StaticString<cInterfaceLen>* host) {
        if (!err.IsNone()) {
            if (auto errDel = mNetIf->DeleteLink(*host); !errDel.IsNone()) {
                LOG_ERR() << "Failed to delete veth" << Log::Field(errDel);
            }
        }
    });

    // Bring the peer up, assign its address and install the default route inside
    // the instance netns in a single namespace entry.
    if (err = mNetIf->ConfigureInstanceInterface(
            params.mContainerIfName, params.mIPWithMask, params.mGateway, params.mNetNSPath);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (params.mHairpin) {
        if (err = mNetIf->SetHairpin(hostName, true); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    result.mHostIfName      = hostName;
    result.mContainerIfName = params.mContainerIfName;

    return ErrorEnum::eNone;
}

Error BridgeNetwork::Detach(const String& instanceID, const String& bridgeIfName)
{
    LOG_DBG() << "Detach bridge" << Log::Field("instanceID", instanceID) << Log::Field("bridge", bridgeIfName);

    const auto hostName = VethName(instanceID, "veth");

    if (auto errDel = mNetIf->DeleteLink(hostName); !errDel.IsNone()) {
        LOG_ERR() << "Failed to delete host veth" << Log::Field(errDel);

        return AOS_ERROR_WRAP(errDel);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

StaticString<cInterfaceLen> BridgeNetwork::VethName(const String& instanceID, const String& prefix)
{
    // FNV-1a 32-bit — deterministic so Detach can recompute without persisting the name.
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < instanceID.Size(); ++i) {
        hash ^= static_cast<uint8_t>(instanceID[i]);
        hash *= 16777619u;
    }

    // Longest prefix "vpeer" + 8 hex = 13 chars, within IFNAMSIZ-1 (15).
    StaticString<cInterfaceLen> name;

    name.Format("%s%08x", prefix.CStr(), hash);

    return name;
}

} // namespace aos::sm::networkmanager
