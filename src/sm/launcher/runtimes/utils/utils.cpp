/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/utils.hpp>

#include "utils.hpp"

namespace aos::sm::launcher::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CreateRuntimeInfo(
    const std::string& runtimeType, const NodeInfo& nodeInfo, size_t maxInstances, RuntimeInfo& runtimeInfo)
{
    const auto runtimeID = std::string(runtimeType).append("-").append(nodeInfo.mNodeID.CStr());

    if (auto err = runtimeInfo.mRuntimeID.Assign(common::utils::NameUUID(runtimeID).c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = runtimeInfo.mRuntimeType.Assign(runtimeType.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    runtimeInfo.mOSInfo = nodeInfo.mOSInfo;

    if (nodeInfo.mCPUs.IsEmpty()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "can't define runtime arch info"));
    }

    runtimeInfo.mArchInfo     = nodeInfo.mCPUs[0].mArchInfo;
    runtimeInfo.mMaxInstances = maxInstances;

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher::utils
