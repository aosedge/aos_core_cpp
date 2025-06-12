/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <filesystem>

#include <aos/common/tools/memory.hpp>

#include "common/logger/logmodule.hpp"
#include "common/utils/exception.hpp"

#include "archivemanager.hpp"

namespace aos::mp::logprovider {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ArchiveManager::Init(sm::logprovider::LogObserverItf& logReceiver, const common::logprovider::Config& config)
{
    LOG_DBG() << "Init archive manager";

    mLogReceiver = &logReceiver;
    mConfig      = config;

    return ErrorEnum::eNone;
}

Error ArchiveManager::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start archive manager";

    if (!mStopped) {
        return Error(ErrorEnum::eRuntime, "already started");
    }

    Poco::TimerCallback<ArchiveManager> callback(*this, &ArchiveManager::CleanupOutdatedArchives);

    mTimer.setStartInterval(cLogCleanupPeriod.Milliseconds());
    mTimer.setPeriodicInterval(cLogCleanupPeriod.Milliseconds());
    mTimer.start(callback);

    mStopped = false;

    mThread = std::thread([this]() { Run(); });

    return ErrorEnum::eNone;
}

Error ArchiveManager::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Stop archive manager";

        if (mStopped) {
            return Error(ErrorEnum::eRuntime, "already stopped");
        }

        mStopped = true;
        mCondVar.notify_all();
    }

    if (mThread.joinable()) {
        mThread.join();
    }

    mTimer.stop();

    return ErrorEnum::eNone;
}

Error ArchiveManager::HandleLog(std::shared_ptr<servicemanager::v4::LogData> log)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Handle log: logID=" << log->log_id().c_str() << ", part=" << log->part()
              << ", status=" << log->status().c_str();

    mLogQueue.push(std::move(log));
    mCondVar.notify_all();

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error ArchiveManager::Archive(std::shared_ptr<servicemanager::v4::LogData> log)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Archive log: logID=" << log->log_id().c_str() << ", part=" << log->part()
              << ", status=" << log->status().c_str();

    auto it = std::find_if(mArchiveContexts.begin(), mArchiveContexts.end(),
        [log](const auto& context) { return context->mLogID == log->log_id(); });

    std::shared_ptr<ArchiveContext> archiveContext = (it == mArchiveContexts.end())
        ? std::make_shared<ArchiveContext>(log->log_id(), *mLogReceiver, mConfig)
        : *it;

    if (GetLogStatus(log) == cloudprotocol::LogStatusEnum::eOk) {
        if (it == mArchiveContexts.end()) {
            mArchiveContexts.push_back(archiveContext);
        }

        archiveContext->mUpdated = Time::Now();

        return archiveContext->mArchivator.AddLog(log->data());
    }

    return SendFinalChunk(archiveContext);
}

void ArchiveManager::Run()
{
    LOG_DBG() << "Run archive manager";

    while (true) {
        std::unique_lock lock {mMutex};

        mCondVar.wait(lock, [this]() { return mStopped || !mLogQueue.empty(); });

        if (mStopped) {
            break;
        }

        auto log = std::move(mLogQueue.front());
        mLogQueue.pop();

        lock.unlock();

        auto err = Archive(log);
        if (!err.IsNone()) {
            LOG_ERR() << "Failed to archive log: err=" << err;
        }
    }
}

// cppcheck-suppress constParameterCallback
void ArchiveManager::CleanupOutdatedArchives(Poco::Timer& timer)
{
    (void)timer;

    std::lock_guard lock {mMutex};

    LOG_DBG() << "Cleanup outdated archives";

    mArchiveContexts.erase(std::remove_if(mArchiveContexts.begin(), mArchiveContexts.end(),
                               [now = Time::Now()](const auto& context) {
                                   auto expiredTime = context->mUpdated.Add(cLogPendingTimeout);

                                   return now > expiredTime;
                               }),
        mArchiveContexts.end());
}

Error ArchiveManager::SendFinalChunk(std::shared_ptr<ArchiveContext> archiveContext)
{
    LOG_DBG() << "Send final chunk: logID=" << archiveContext->mLogID.c_str();

    mArchiveContexts.erase(
        std::remove(mArchiveContexts.begin(), mArchiveContexts.end(), archiveContext), mArchiveContexts.end());

    return archiveContext->mArchivator.SendLog(archiveContext->mLogID.c_str());
}

cloudprotocol::LogStatus ArchiveManager::GetLogStatus(const std::shared_ptr<servicemanager::v4::LogData>& log) const
{
    cloudprotocol::LogStatus status;

    status.FromString(log->status().c_str());

    return status;
}

} // namespace aos::mp::logprovider
