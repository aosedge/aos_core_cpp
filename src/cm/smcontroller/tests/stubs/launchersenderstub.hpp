/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_LAUNCHER_SENDERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_LAUNCHER_SENDERSTUB_HPP_

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <core/cm/launcher/itf/sender.hpp>

namespace aos::cm::launcher {

/**
 * Env vars sender stub.
 */
class SenderStub : public SenderItf {
public:
    Error SendOverrideEnvsStatuses(const OverrideEnvVarsStatuses& statuses) override
    {
        std::lock_guard lock {mMutex};

        for (const auto& status : statuses.mStatuses) {
            mStatuses.push_back(status);
        }

        mCV.notify_all();

        return ErrorEnum::eNone;
    }

    Error WaitEnvVarStatus(const InstanceIdent& instanceIdent, const String& varName)
    {
        std::unique_lock lock {mMutex};

        auto hasStatus = [this, &instanceIdent, &varName]() { return HasEnvVarStatusLocked(instanceIdent, varName); };

        bool received = mCV.wait_for(lock, cDefaultTimeout, hasStatus);
        if (!received) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait env var status timeout"));
        }

        return ErrorEnum::eNone;
    }

private:
    bool HasEnvVarStatusLocked(const InstanceIdent& instanceIdent, const String& varName) const
    {
        auto it
            = std::find_if(mStatuses.begin(), mStatuses.end(), [&instanceIdent](const EnvVarsInstanceStatus& status) {
                  return static_cast<const InstanceIdent&>(status) == instanceIdent;
              });

        if (it == mStatuses.end()) {
            return false;
        }

        auto varIt = std::find_if(it->mStatuses.begin(), it->mStatuses.end(),
            [&varName](const EnvVarStatus& varStatus) { return varStatus.mName == varName; });

        return varIt != it->mStatuses.end();
    }

    static constexpr auto cDefaultTimeout = std::chrono::seconds(1);

    std::vector<EnvVarsInstanceStatus> mStatuses;
    mutable std::mutex                 mMutex;
    std::condition_variable            mCV;
};

} // namespace aos::cm::launcher

#endif
