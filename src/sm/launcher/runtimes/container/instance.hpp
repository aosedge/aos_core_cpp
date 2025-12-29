/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_INSTANCE_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_INSTANCE_HPP_

#include <core/common/types/instance.hpp>

namespace aos::sm::launcher {

/**
 * Launcher instance.
 */
class Instance {
public:
    /**
     * Constructor.
     *
     * @param instanceInfo instance info.
     */
    Instance(const InstanceInfo& instance);

    /**
     * Starts instance.
     *
     * @return Error
     */
    Error Start();

    /**
     * Stops instance.
     *
     * @return Error
     */
    Error Stop();

    /**
     * Returns instance ID.
     *
     * @return std::string.
     */
    std::string InstanceID() const { return mInstanceID; }

    /**
     * Returns instance status.
     *
     * @param[out] status instance status.
     */
    void GetStatus(InstanceStatus& status) const;

private:
    void GenerateInstanceID();

    InstanceInfo mInstanceInfo;
    std::string  mInstanceID;
};

} // namespace aos::sm::launcher

#endif
