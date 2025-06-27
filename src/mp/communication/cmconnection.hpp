/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_COMMUNICATION_CMCONNECTION_HPP_
#define AOS_MP_COMMUNICATION_CMCONNECTION_HPP_

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <Poco/Runnable.h>
#include <Poco/TaskManager.h>
#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include <aos/common/downloader/downloader.hpp>
#include <aos/common/tools/error.hpp>

#include <common/downloader/downloader.hpp>
#include <mp/config/config.hpp>
#include <mp/filechunker/filechunker.hpp>
#include <mp/imageunpacker/imageunpacker.hpp>
#include <mp/logprovider/archivemanager.hpp>

#include "types.hpp"

namespace aos::mp::communication {

/**
 * CM connection class.
 */
class CMConnection : public sm::logprovider::LogObserverItf {
public:
    /**
     * Constructor.
     */
    CMConnection();

    /**
     * Initializes connection.
     *
     * @param cfg Configuration.
     * @param certProvider Certificate provider.
     * @param comManager Communication manager.
     * @param channel Channel.
     * @return Error.
     */
    Error Init(const config::Config& cfg, HandlerItf& handler, CommunicationManagerItf& comManager,
        downloader::DownloaderItf* downloader = nullptr, common::iamclient::TLSCredentialsItf* certProvider = nullptr);

    /**
     * Starts the connection.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops the connection.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * On log received event handler.
     *
     * @param log log.
     * @return Error.
     */
    Error OnLogReceived(const cloudprotocol::PushLog& log) override;

private:
    static constexpr auto cConnectionTimeout = std::chrono::seconds(3);

    class Task : public Poco::Task {
    public:
        using Callback = std::function<void()>;

        explicit Task(Callback callback)
            : Poco::Task(generateTaskName())
            , mCallback(std::move(callback))
        {
        }

        // cppcheck-suppress unusedFunction
        void runTask() override { mCallback(); }

    private:
        static std::string generateTaskName()
        {
            static Poco::UUIDGenerator mUuidGenerator;

            return mUuidGenerator.create().toString();
        }

        Callback mCallback;
    };

    std::future<void> StartTaskWithWait(std::function<void()> func)
    {
        auto              promise = std::make_shared<std::promise<void>>();
        std::future<void> future  = promise->get_future();

        auto task = new Task([func, promise]() {
            func();
            promise->set_value();
        });

        mTaskManager.start(task);

        return future;
    }

    void StartTask(std::function<void()> func) { mTaskManager.start(new Task(std::move(func))); }

    void  RunSecureChannel();
    void  RunOpenChannel();
    Error ReadSecureMsgHandler();
    Error ReadOpenMsgHandler();
    void  WriteSecureMsgHandler();

    Error SendSMClockSync();

    Error Download(const std::string& url, uint64_t requestID, const std::string& contentType);
    Error SendFailedImageContentResponse(uint64_t requestID, const Error& err);
    Error SendImageContentInfo(const filechunker::ContentInfo& contentInfo);
    RetWithError<filechunker::ContentInfo> GetFileContent(
        const std::string& url, uint64_t requestID, const std::string& contentType);

    Error SendMessage(std::vector<uint8_t> message, std::shared_ptr<CommChannelItf>& channel);
    RetWithError<std::vector<uint8_t>> ReadMessage(std::shared_ptr<CommChannelItf>& channel);

    std::shared_ptr<CommChannelItf> mCMCommOpenChannel;
    std::shared_ptr<CommChannelItf> mCMCommSecureChannel;
    HandlerItf*                     mHandler {};

    Poco::TaskManager mTaskManager;

    downloader::DownloaderItf*                  mDownloader {};
    std::string                                 mDownloadDir;
    std::optional<imageunpacker::ImageUnpacker> mImageUnpacker;

    std::atomic<bool>           mShutdown {};
    std::mutex                  mMutex;
    std::condition_variable     mCondVar;
    logprovider::ArchiveManager mArchiveManager;
};

} // namespace aos::mp::communication

#endif
