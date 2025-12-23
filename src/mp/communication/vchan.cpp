/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/logger/logmodule.hpp>

#include "vchan.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error VChan::Init(const config::VChanConfig& config)
{
    LOG_DBG() << "Init the virtual channel";

    mConfig = config;

    return ErrorEnum::eNone;
}

Error VChan::Connect()
{
    std::lock_guard lock {mMutex};

    if (mShutdown) {
        return ErrorEnum::eFailed;
    }

    if (mConnected) {
        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Connect to the virtual channel";

    if (auto err = ConnectToVChan(mVChanRead, mConfig.mXSRXPath, mConfig.mDomain); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ConnectToVChan(mVChanWrite, mConfig.mXSTXPath, mConfig.mDomain); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mConnected = true;

    return ErrorEnum::eNone;
}

Error VChan::Read(std::vector<uint8_t>& message)
{
    int read {};

    while (read < static_cast<int>(message.size())) {
        int len = libxenvchan_read(mVChanRead, message.data() + read, message.size() - read);
        if (len < 0) {
            return len;
        }

        read += len;
    }

    return ErrorEnum::eNone;
}

Error VChan::Write(std::vector<uint8_t> message)
{
    int written {};

    while (written < static_cast<int>(message.size())) {
        int len = libxenvchan_write(mVChanWrite, message.data() + written, message.size() - written);
        if (len < 0) {
            return len;
        }

        written += len;
    }

    return ErrorEnum::eNone;
}

Error VChan::Close()
{
    std::lock_guard lock {mMutex};

    if (!mConnected || mShutdown) {
        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Close virtual channel";

    libxenvchan_close(mVChanRead);
    libxenvchan_close(mVChanWrite);

    mConnected = false;

    return ErrorEnum::eNone;
}

Error VChan::Shutdown()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Shutting down socket";

    mShutdown = true;

    if (mConnected) {
        libxenvchan_close(mVChanRead);
        libxenvchan_close(mVChanWrite);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error VChan::ConnectToVChan(struct libxenvchan*& vchan, const std::string& path, int domain)
{
    vchan = libxenvchan_server_init(nullptr, domain, path.c_str(), 0, 0);
    if (vchan == nullptr) {
        return Error(ErrorEnum::eFailed, errno != 0 ? strerror(errno) : "failed to connect");
    }

    vchan->blocking = 0x1;

    return ErrorEnum::eNone;
}

} // namespace aos::mp::communication
