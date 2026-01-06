/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "runtimeconfig.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

RetWithError<StaticString<cEnvVarNameLen>> GetEnvVarName(const String& envVar)
{
    StaticString<cEnvVarLen> tmpStr = envVar;

    if (auto result = tmpStr.FindSubstr(0, "="); result.mError.IsNone()) {
        if (auto err = tmpStr.Resize(result.mValue); !err.IsNone()) {
            return {0, AOS_ERROR_WRAP(err)};
        }
    }

    tmpStr.Trim(" ");

    return StaticString<cEnvVarNameLen>(tmpStr);
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error AddMount(const Mount& mount, oci::RuntimeConfig& runtimeConfig)
{
    auto existMount = runtimeConfig.mMounts.FindIf(
        [&destination = mount.mDestination](const Mount& mount) { return mount.mDestination == destination; });
    if (existMount == runtimeConfig.mMounts.end()) {
        if (auto err = runtimeConfig.mMounts.PushBack(mount); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        existMount = &runtimeConfig.mMounts[runtimeConfig.mMounts.Size() - 1];
    } else {
        *existMount = mount;
    }

    if (existMount->mType.IsEmpty()) {
        existMount->mType = "bind";
    }

    return ErrorEnum::eNone;
}

// cppcheck-suppress constParameterReference
Error AddNamespace(const oci::LinuxNamespace& ns, oci::RuntimeConfig& runtimeConfig)
{
    auto existNS = runtimeConfig.mLinux->mNamespaces.FindIf(
        [&nsType = ns.mType](const oci::LinuxNamespace& ns) { return ns.mType == nsType; });
    if (existNS == runtimeConfig.mLinux->mNamespaces.end()) {
        if (auto err = runtimeConfig.mLinux->mNamespaces.PushBack(ns); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } else {
        *existNS = ns;
    }

    return ErrorEnum::eNone;
}

// cppcheck-suppress constParameterReference
Error AddEnvVars(const Array<StaticString<cEnvVarLen>>& envVars, oci::RuntimeConfig& runtimeConfig)
{
    for (const auto& newEnvVar : envVars) {
        StaticString<cEnvVarNameLen> newEnvVarName;
        Error                        err;

        Tie(newEnvVarName, err) = GetEnvVarName(newEnvVar);
        if (!err.IsNone()) {
            return err;
        }

        auto updated = false;

        for (auto& existingEnvVar : runtimeConfig.mProcess->mEnv) {
            StaticString<cEnvVarNameLen> existingEnvVarName;

            Tie(existingEnvVarName, err) = GetEnvVarName(existingEnvVar);
            if (!err.IsNone()) {
                return err;
            }

            if (newEnvVarName == existingEnvVarName) {
                existingEnvVar = newEnvVar;
                updated        = true;

                break;
            }
        }

        if (!updated) {
            if (err = runtimeConfig.mProcess->mEnv.PushBack(newEnvVar); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    }

    return ErrorEnum::eNone;
}

// cppcheck-suppress constParameterReference
Error SetCPULimit(int64_t quota, uint64_t period, oci::RuntimeConfig& runtimeConfig)
{
    if (!runtimeConfig.mLinux->mResources->mCPU.HasValue()) {
        runtimeConfig.mLinux->mResources->mCPU.EmplaceValue();
    }

    runtimeConfig.mLinux->mResources->mCPU->mPeriod = period;
    runtimeConfig.mLinux->mResources->mCPU->mQuota  = quota;

    return ErrorEnum::eNone;
}

// cppcheck-suppress constParameterReference
Error SetRAMLimit(int64_t limit, oci::RuntimeConfig& runtimeConfig)
{
    if (!runtimeConfig.mLinux->mResources->mMemory.HasValue()) {
        runtimeConfig.mLinux->mResources->mMemory.EmplaceValue();
    }

    runtimeConfig.mLinux->mResources->mMemory->mLimit = limit;

    return ErrorEnum::eNone;
}

// cppcheck-suppress constParameterReference
Error SetPIDLimit(int64_t limit, oci::RuntimeConfig& runtimeConfig)
{
    if (!runtimeConfig.mLinux->mResources->mPids.HasValue()) {
        runtimeConfig.mLinux->mResources->mPids.EmplaceValue();
    }

    runtimeConfig.mLinux->mResources->mPids->mLimit = limit;

    return ErrorEnum::eNone;
}

// cppcheck-suppress constParameterReference
Error AddRLimit(const oci::POSIXRlimit& rlimit, oci::RuntimeConfig& runtimeConfig)
{
    auto existRlimit = runtimeConfig.mProcess->mRlimits.FindIf(
        [&type = rlimit.mType](const oci::POSIXRlimit& rlimit) { return rlimit.mType == type; });
    if (existRlimit == runtimeConfig.mProcess->mRlimits.end()) {
        if (auto err = runtimeConfig.mProcess->mRlimits.PushBack(rlimit); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } else {
        *existRlimit = rlimit;
    }

    return ErrorEnum::eNone;
}

// cppcheck-suppress constParameterReference
Error AddAdditionalGID(uint32_t gid, oci::RuntimeConfig& runtimeConfig)
{
    if (runtimeConfig.mProcess->mUser.mAdditionalGIDs.Find(gid)
        != runtimeConfig.mProcess->mUser.mAdditionalGIDs.end()) {
        return ErrorEnum::eNone;
    }

    if (auto err = runtimeConfig.mProcess->mUser.mAdditionalGIDs.PushBack(gid); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error AddDevice(const oci::LinuxDevice& device, const StaticString<cPermissionsLen>& permissions,
    oci::RuntimeConfig& runtimeConfig) // cppcheck-suppress constParameterReference
{
    auto existDevice = runtimeConfig.mLinux->mDevices.FindIf(
        [&path = device.mPath](const oci::LinuxDevice& device) { return device.mPath == path; });
    if (existDevice == runtimeConfig.mLinux->mDevices.end()) {
        if (auto err = runtimeConfig.mLinux->mDevices.PushBack(device); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } else {
        *existDevice = device;
    }

    auto existCgroup
        = runtimeConfig.mLinux->mResources->mDevices.FindIf([&device](const oci::LinuxDeviceCgroup& cgroupDevice) {
              return cgroupDevice.mType == device.mType && cgroupDevice.mMajor == device.mMajor
                  && cgroupDevice.mMinor == device.mMinor;
          });
    if (existCgroup == runtimeConfig.mLinux->mResources->mDevices.end()) {
        if (auto err = runtimeConfig.mLinux->mResources->mDevices.EmplaceBack(
                device.mType, permissions, true, device.mMajor, device.mMinor);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } else {
        existCgroup->mAllow  = true;
        existCgroup->mAccess = permissions;
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
