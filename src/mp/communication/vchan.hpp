/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_COMMUNICATION_VCHAN_HPP_
#define AOS_MP_COMMUNICATION_VCHAN_HPP_

extern "C" {
#include <libxenvchan.h>
}

#include <mutex>

#include <mp/config/config.hpp>

#include "types.hpp"

namespace aos::mp::communication {

/**
 * Virtual Channel class.
 */
class VChan : public TransportItf {
public:
    /**
     * Initializes the virtual channel.
     *
     * @param config configuration.
     * @return Error.
     */
    Error Init(const config::VChanConfig& config);

    /**
     * Connects to the virtual channel.
     *
     * @return Error.
     */
    Error Connect() override;

    /**
     * Reads message from the virtual channel.
     *
     * @param message[out] read message.
     * @return Error.
     */
    Error Read(std::vector<uint8_t>& message) override;

    /**
     * Writes message to the virtual channel.
     *
     * @param message message to write.
     * @return Error.
     */
    Error Write(std::vector<uint8_t> message) override;

    /**
     * Closes the virtual channel.
     *
     * @return Error.
     */
    Error Close() override;

    /**
     * Shuts down virtual channel.
     *
     * @return Error.
     */
    Error Shutdown() override;

private:
    Error ConnectToVChan(struct libxenvchan*& vchan, const std::string& path, int domain);

    struct libxenvchan* mVChanRead {};
    struct libxenvchan* mVChanWrite {};
    config::VChanConfig mConfig {};
    bool                mShutdown {};
    bool                mConnected {};
    std::mutex          mMutex;
};

} // namespace aos::mp::communication

#endif
