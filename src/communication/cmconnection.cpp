/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/ThreadPool.h>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>
#include <logger/logmodule.hpp>
#include <servicemanager/v4/servicemanager.grpc.pb.h>

#include "cmconnection.hpp"
#include "communication/utils.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

CMConnection::CMConnection()
    : mTaskManager(Poco::ThreadPool::defaultPool())
{
}

Error CMConnection::Init(const config::Config& cfg, HandlerItf& handler, CommunicationManagerItf& comManager,
    common::iamclient::TLSCredentialsItf* certProvider)
{
    LOG_DBG() << "Init CM connection";

    mHandler = &handler;

    try {
        mCMCommOpenChannel = comManager.CreateChannel(cfg.mCMConfig.mOpenPort);
        if (certProvider != nullptr) {
            LOG_DBG() << "Create CM secure channel port=" << cfg.mCMConfig.mSecurePort
                      << ", certStorage=" << cfg.mVChan.mSMCertStorage.c_str();

            mCMCommSecureChannel
                = comManager.CreateChannel(cfg.mCMConfig.mSecurePort, certProvider, cfg.mVChan.mSMCertStorage);
            mDownloader.emplace(cfg.mDownload.mDownloadDir);
            mImageUnpacker.emplace(cfg.mImageStoreDir);
        }
    } catch (const std::exception& e) {
        return Error(ErrorEnum::eFailed, e.what());
    }

    StartTask([this] { RunOpenChannel(); });
    StartTask([this] { RunSecureChannel(); });

    return ErrorEnum::eNone;
}

void CMConnection::Close()
{
    {
        std::lock_guard lock {mMutex};

        if (mShutdown) {
            return;
        }

        LOG_DBG() << "Close CM connection";

        mShutdown = true;

        mCMCommOpenChannel->Close();

        if (mCMCommSecureChannel != nullptr) {
            mHandler->OnDisconnected();
            mCMCommSecureChannel->Close();
        }
    }

    mCondVar.notify_all();

    mTaskManager.cancelAll();
    mTaskManager.joinAll();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void CMConnection::RunSecureChannel()
{
    if (mCMCommSecureChannel == nullptr) {
        return;
    }

    LOG_DBG() << "Run CM secure channel";

    auto writeFuture = StartTaskWithWait([this]() { WriteSecureMsgHandler(); });

    while (!mShutdown) {
        if (auto err = mCMCommSecureChannel->Connect(); !err.IsNone()) {
            std::unique_lock lock {mMutex};

            LOG_WRN() << "Failed to connect: error=" << err;

            mCondVar.wait_for(lock, cConnectionTimeout, [this]() { return mShutdown.load(); });

            continue;
        }

        mHandler->OnConnected();
        mCondVar.notify_all();

        if (auto err = ReadSecureMsgHandler(); !err.IsNone()) {
            LOG_ERR() << "Failed to read secure message: error=" << err;
        }

        if (auto err = mCMCommSecureChannel->Close(); !err.IsNone()) {
            LOG_ERR() << "Failed to close secure channel: error=" << err;
        }

        LOG_DBG() << "Secure CM channel disconnected";
    }

    writeFuture.wait();

    LOG_DBG() << "Secure channel stopped";
}

void CMConnection::RunOpenChannel()
{
    LOG_DBG() << "Run CM open channel";

    while (!mShutdown) {
        if (auto err = mCMCommOpenChannel->Connect(); !err.IsNone()) {
            std::unique_lock lock {mMutex};

            LOG_WRN() << "Failed to connect CM: error=" << err;

            mCondVar.wait_for(lock, cConnectionTimeout, [this]() { return mShutdown.load(); });

            continue;
        }

        if (auto err = ReadOpenMsgHandler(); !err.IsNone()) {
            LOG_ERR() << "Failed to read open message: error=" << err;
        }
    }

    LOG_DBG() << "Open channel stopped";
}

Error CMConnection::ReadSecureMsgHandler()
{
    LOG_DBG() << "Read secure message handler";

    while (!mShutdown) {
        auto [message, err] = ReadMessage(mCMCommSecureChannel);
        if (!err.IsNone()) {
            return err;
        }

        servicemanager::v4::SMOutgoingMessages outgoingMessages;
        if (!outgoingMessages.ParseFromArray(message.data(), message.size())) {
            LOG_ERR() << "Failed to parse message";

            continue;
        }

        if (outgoingMessages.has_image_content_request()) {
            LOG_DBG() << "Image content request received";

            StartTask([this, url = outgoingMessages.image_content_request().url(),
                          requestID   = outgoingMessages.image_content_request().request_id(),
                          contentType = outgoingMessages.image_content_request().content_type()] {
                if (auto err = Download(url, requestID, contentType); !err.IsNone()) {
                    LOG_ERR() << "Failed to download: error=" << err;
                }
            });

            continue;
        }

        if (err = mHandler->SendMessages(std::move(message)); !err.IsNone()) {
            return err;
        }

        LOG_DBG() << "Message sent to CM";
    }

    return ErrorEnum::eNone;
}

Error CMConnection::SendFailedImageContentResponse(uint64_t requestID, const Error& err)
{
    LOG_ERR() << "Send failed image content response: requestID=" << requestID << ", error=" << err;

    servicemanager::v4::SMIncomingMessages incomingMessages;

    auto imageContentInfo = incomingMessages.mutable_image_content_info();
    auto errorInfo        = imageContentInfo->mutable_error();
    errorInfo->set_aos_code(static_cast<int>(err.Value()));
    errorInfo->set_message(err.Message());
    imageContentInfo->set_request_id(requestID);

    std::vector<uint8_t> data(incomingMessages.ByteSizeLong());

    if (!incomingMessages.SerializeToArray(data.data(), static_cast<int>(data.size()))) {
        return Error(ErrorEnum::eFailed, "failed to serialize message");
    }

    if (auto errSend = SendMessage(std::move(data), mCMCommSecureChannel); !errSend.IsNone()) {
        return errSend;
    }

    return ErrorEnum::eNone;
}

Error CMConnection::Download(const std::string& url, uint64_t requestID, const std::string& contentType)
{
    LOG_DBG() << "Download: url=" << url.c_str() << ", requestID=" << requestID
              << ", contentType=" << contentType.c_str();

    auto [fileName, err] = mDownloader->Download(url);
    if (!err.IsNone()) {
        if (auto errSend = SendFailedImageContentResponse(requestID, err); !errSend.IsNone()) {
            return errSend;
        }

        return err;
    }

    auto [contentInfo, errContent] = GetFileContent(fileName, requestID, contentType);
    if (!errContent.IsNone()) {
        if (auto errSend = SendFailedImageContentResponse(requestID, errContent); !errSend.IsNone()) {
            return errSend;
        }

        return errContent;
    }

    if (err = SendImageContentInfo(contentInfo); !err.IsNone()) {
        if (auto errSend = SendFailedImageContentResponse(requestID, err); !errSend.IsNone()) {
            return errSend;
        }

        return err;
    }

    LOG_DBG() << "Image content sent: requestID=" << requestID;

    return ErrorEnum::eNone;
}

Error CMConnection::SendImageContentInfo(const filechunker::ContentInfo& contentInfo)
{
    servicemanager::v4::SMIncomingMessages incomingMessages;

    auto imageContentInfo = incomingMessages.mutable_image_content_info();
    imageContentInfo->set_request_id(contentInfo.mRequestID);

    for (const auto& imageFile : contentInfo.mImageFiles) {
        auto imageFileProto = imageContentInfo->add_image_files();

        LOG_DBG() << "Send image file: relativePath=" << imageFile.mRelativePath.c_str();

        imageFileProto->set_relative_path(imageFile.mRelativePath);
        imageFileProto->set_sha256(imageFile.mSha256.data(), imageFile.mSha256.size());
        imageFileProto->set_size(imageFile.mSize);
    }

    std::vector<uint8_t> data(incomingMessages.ByteSizeLong());

    if (!incomingMessages.SerializeToArray(data.data(), static_cast<int>(data.size()))) {
        return Error(ErrorEnum::eFailed, "failed to serialize image content info");
    }

    if (auto err = SendMessage(std::move(data), mCMCommSecureChannel); !err.IsNone()) {
        return err;
    }

    for (const auto& imageContent : contentInfo.mImageContents) {
        incomingMessages.Clear();

        auto imageContentProto = incomingMessages.mutable_image_content();
        imageContentProto->set_request_id(imageContent.mRequestID);
        imageContentProto->set_relative_path(imageContent.mRelativePath);
        imageContentProto->set_parts_count(imageContent.mPartsCount);
        imageContentProto->set_part(imageContent.mPart);
        imageContentProto->set_data(imageContent.mData.data(), imageContent.mData.size());

        data.resize(incomingMessages.ByteSizeLong());

        if (!incomingMessages.SerializeToArray(data.data(), static_cast<int>(data.size()))) {
            return Error(ErrorEnum::eFailed, "failed to serialize image content");
        }

        if (auto err = SendMessage(std::move(data), mCMCommSecureChannel); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

RetWithError<filechunker::ContentInfo> CMConnection::GetFileContent(
    const std::string& url, uint64_t requestID, const std::string& contentType)
{
    auto [unpackedDir, err] = mImageUnpacker->Unpack(url, contentType);
    if (!err.IsNone()) {
        return {filechunker::ContentInfo(), err};
    }

    LOG_DBG() << "Unpacked image: unpackedDir=" << unpackedDir.c_str() << " requestID=" << requestID;

    return filechunker::ChunkFiles(unpackedDir, requestID);
}

Error CMConnection::ReadOpenMsgHandler()
{
    LOG_DBG() << "Read open message handler";

    while (!mShutdown) {
        auto [message, err] = ReadMessage(mCMCommOpenChannel);
        if (!err.IsNone()) {
            return err;
        }

        servicemanager::v4::SMOutgoingMessages outgoingMessages;
        if (!outgoingMessages.ParseFromArray(message.data(), message.size())) {
            LOG_ERR() << "Failed to parse open message";

            continue;
        }

        if (outgoingMessages.has_clock_sync_request()) {
            if (err = SendSMClockSync(); !err.IsNone()) {
                LOG_ERR() << "Failed to send clock sync: error=" << err;
            }

            continue;
        }

        if (err = mHandler->SendMessages(std::move(message)); !err.IsNone()) {
            return err;
        }

        LOG_DBG() << "Message sent to CM";
    }

    return ErrorEnum::eNone;
}

Error CMConnection::SendSMClockSync()
{
    LOG_DBG() << "Send clock sync";

    servicemanager::v4::SMIncomingMessages incomingMessages;

    auto clockSync                     = incomingMessages.mutable_clock_sync();
    *clockSync->mutable_current_time() = google::protobuf::util::TimeUtil::GetCurrentTime();

    std::vector<uint8_t> data(incomingMessages.ByteSizeLong());

    if (!incomingMessages.SerializeToArray(data.data(), static_cast<int>(data.size()))) {
        return Error(ErrorEnum::eFailed, "failed to serialize message");
    }

    if (auto err = SendMessage(std::move(data), mCMCommOpenChannel); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

void CMConnection::WriteSecureMsgHandler()
{
    LOG_DBG() << "Write secure message handler";

    while (!mShutdown) {
        auto message = mHandler->ReceiveMessages();
        if (!message.mError.IsNone()) {
            LOG_ERR() << "Failed to receive message error=" << message.mError;

            continue;
        }

        LOG_DBG() << "Received message from CM: size=" << message.mValue.size();

        {
            std::unique_lock lock {mMutex};

            mCondVar.wait(lock, [this] { return mCMCommSecureChannel->IsConnected() || mShutdown; });
            if (mShutdown) {
                return;
            }
        }

        if (auto err = SendMessage(std::move(message.mValue), mCMCommSecureChannel); !err.IsNone()) {
            LOG_ERR() << "Failed to write secure message error=" << err;

            continue;
        }
    }
}

Error CMConnection::SendMessage(std::vector<uint8_t> message, std::shared_ptr<CommChannelItf>& channel)
{
    auto protobufHeader = PrepareProtobufHeader(message.size());
    protobufHeader.insert(protobufHeader.end(), message.begin(), message.end());

    return channel->Write(std::move(protobufHeader));
}

RetWithError<std::vector<uint8_t>> CMConnection::ReadMessage(std::shared_ptr<CommChannelItf>& channel)
{
    std::vector<uint8_t> message(cProtobufHeaderSize);
    if (auto err = channel->Read(message); !err.IsNone()) {
        return {std::vector<uint8_t>(), err};
    }

    auto protobufHeader = ParseProtobufHeader(message);
    message.clear();
    message.resize(protobufHeader.mDataSize);

    if (auto err = channel->Read(message); !err.IsNone()) {
        return {std::vector<uint8_t>(), err};
    }

    return {message, ErrorEnum::eNone};
}

} // namespace aos::mp::communication
