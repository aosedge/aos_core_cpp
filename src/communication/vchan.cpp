/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "logger/logmodule.hpp"

#include "vchan.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error VChan::Init(const VChanConfig& config)
{
    LOG_DBG() << "Initialize the virtual channel";

    mConfig = config;

    return ErrorEnum::eNone;
}

Error VChan::Connect()
{
    if (mShutdown) {
        return ErrorEnum::eFailed;
    }

    LOG_DBG() << "Connect to the virtual channel";

    if (auto err = ConnectToVChan(mVChanRead, mConfig.mXSRXPath, mConfig.mDomain); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ConnectToVChan(mVChanWrite, mConfig.mXSTXPath, mConfig.mDomain);
}

Error VChan::Read(std::vector<uint8_t>& message)
{
    LOG_DBG() << "Read from virtual channel: expectedSize=" << message.size();

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
    LOG_DBG() << "Write to virtual channel: size=" << message.size();

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

aos::Error VChan::Close()
{
    LOG_DBG() << "Close virtual channel";

    libxenvchan_close(mVChanRead);
    libxenvchan_close(mVChanWrite);

    mShutdown = true;

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error VChan::ConnectToVChan(struct libxenvchan*& vchan, const std::string& path, int domain)
{
    vchan = libxenvchan_server_init(nullptr, domain, path.c_str(), 0, 0);
    if (vchan == nullptr) {
        return Error(aos::ErrorEnum::eFailed, errno != 0 ? strerror(errno) : "failed to connect");
    }

    vchan->blocking = 0x1;

    return ErrorEnum::eNone;
}

} // namespace aos::mp::communication
