/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_HPP_

#include <memory>
#include <vector>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/iamclient/itf/permhandler.hpp>
#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/sm/imagemanager/itf/iteminfoprovider.hpp>
#include <core/sm/launcher/itf/instancestatusreceiver.hpp>
#include <core/sm/launcher/itf/runtime.hpp>
#include <core/sm/networkmanager/itf/networkmanager.hpp>

#include <sm/utils/itf/systemdconn.hpp>

#include "config.hpp"

namespace aos::sm::launcher {

/**
 * Runtimes factory.
 */
class Runtimes {
public:
    /**
     * Initializes runtimes.
     *
     * @param config runtime config.
     * @param currentNodeInfoProvider current node info provider.
     * @param itemInfoProvider item info provider.
     * @param networkManager network manager.
     * @param permHandler permission handler.
     * @param ociSpec OCI spec interface.
     * @param statusReceiver instance status receiver.
     * @param systemdConn systemd connection.
     * @return Error.
     */
    Error Init(const Config& config, aos::iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
        imagemanager::ItemInfoProviderItf& itemInfoProvider, networkmanager::NetworkManagerItf& networkManager,
        aos::iamclient::PermHandlerItf& permHandler, oci::OCISpecItf& ociSpec,
        InstanceStatusReceiverItf& statusReceiver, utils::SystemdConnItf& systemdConn);

    /**
     * Returns runtimes.
     *
     * @param[out] runtimes array to store runtimes.
     * @return Error.
     */
    Error GetRuntimes(Array<RuntimeItf*>& runtimes) const;

private:
    std::vector<std::unique_ptr<RuntimeItf>> mRuntimes;
};

} // namespace aos::sm::launcher

#endif
