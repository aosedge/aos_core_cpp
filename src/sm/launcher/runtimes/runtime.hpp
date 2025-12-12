/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_RUNTIME_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_RUNTIME_HPP_

#include <memory>
#include <vector>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/sm/launcher/itf/instancestatusreceiver.hpp>
#include <core/sm/launcher/itf/rebooter.hpp>

#include <sm/launcher/runtimes/utils/systemdrebooter.hpp>
#include <sm/launcher/runtimes/utils/systemdupdatechecker.hpp>

#include "config.hpp"
#include "imagemanager.hpp"

namespace aos::sm::launcher {

/**
 * Runtime factory.
 */
class Runtimes {
public:
    /**
     * Initializes runtimes.
     *
     * @param config runtime config.
     * @param currentNodeInfoProvider current node info provider.
     * @param imageManager image manager.
     * @param ociSpec OCI spec.
     * @param statusReceiver instance status receiver.
     * @param systemdConn systemd connection.
     * @return Error.
     */
    Error Init(const Config& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
        ImageManagerItf& imageManager, oci::OCISpecItf& ociSpec, InstanceStatusReceiverItf& statusReceiver,
        sm::utils::SystemdConnItf& systemdConn);

    /**
     * Returns runtimes.
     *
     * @return StaticArray<RuntimeItf*, cMaxNumNodeRuntimes>
     */
    StaticArray<RuntimeItf*, cMaxNumNodeRuntimes> GetRuntimes() const;

private:
    Error InitRootfsRuntime(const RootfsConfig& config);
    Error StoreRuntime(std::unique_ptr<RuntimeItf>&& runtime);

    iamclient::CurrentNodeInfoProviderItf*   mCurrentNodeInfoProvider {};
    ImageManagerItf*                         mImageManager {};
    oci::OCISpecItf*                         mOciSpec {};
    InstanceStatusReceiverItf*               mStatusReceiver {};
    sm::utils::SystemdConnItf*               mSystemdConn {};
    std::vector<std::unique_ptr<RuntimeItf>> mRuntimes;
    utils::SystemdUpdateChecker              mRootfsUpdateChecker;
    utils::SystemdRebooter                   mRebooter;
};

} // namespace aos::sm::launcher

#endif
