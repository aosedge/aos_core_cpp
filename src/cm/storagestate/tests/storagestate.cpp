/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>

#include <gtest/gtest.h>

#include <core/cm/tests/mocks/communicationmock.hpp>
#include <core/common/tests/mocks/fsmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/common/tools/fs.hpp>

#include <cm/storagestate/storagestate.hpp>

using namespace testing;

namespace aos::cm::storagestate {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

const auto cTestDir    = std::filesystem::path("storage_state");
const auto cStorageDir = cTestDir / "storage";
const auto cStateDir   = cTestDir / "state";

/***********************************************************************************************************************
 * Stubs
 **********************************************************************************************************************/

class StorageStub : public aos::cm::storagestate::StorageItf {
public:
    Error AddStorageStateInfo(const StorageStateInstanceInfo& storageStateInfo) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Add storage state info" << Log::Field("instanceIdent", storageStateInfo.mInstanceIdent);

        auto it = mStorageStateInfos.find(storageStateInfo.mInstanceIdent);
        if (it != mStorageStateInfos.end()) {
            return ErrorEnum::eAlreadyExist;
        }

        mStorageStateInfos[storageStateInfo.mInstanceIdent] = storageStateInfo;

        return ErrorEnum::eNone;
    }

    Error RemoveStorageStateInfo(const InstanceIdent& instanceIdent) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Remove storage state info" << Log::Field("instanceIdent", instanceIdent);

        auto it = mStorageStateInfos.find(instanceIdent);
        if (it == mStorageStateInfos.end()) {
            return ErrorEnum::eNotFound;
        }

        mStorageStateInfos.erase(it);

        return ErrorEnum::eNone;
    }

    Error GetAllStorageStateInfo(Array<StorageStateInstanceInfo>& storageStateInfos) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Get all storage state infos";

        for (const auto& [instanceIdent, storageStateInfo] : mStorageStateInfos) {
            if (auto err = storageStateInfos.PushBack(storageStateInfo); !err.IsNone()) {
                return err;
            }
        }

        return ErrorEnum::eNone;
    }

    Error GetStorageStateInfo(const InstanceIdent& instanceIdent, StorageStateInstanceInfo& storageStateInfo) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Get storage state info" << Log::Field("instanceIdent", instanceIdent);

        auto it = mStorageStateInfos.find(instanceIdent);
        if (it == mStorageStateInfos.end()) {
            return ErrorEnum::eNotFound;
        }

        storageStateInfo = it->second;

        return ErrorEnum::eNone;
    }

    Error UpdateStorageStateInfo(const StorageStateInstanceInfo& storageStateInfo) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Update storage state info" << Log::Field("instanceIdent", storageStateInfo.mInstanceIdent);

        auto it = mStorageStateInfos.find(storageStateInfo.mInstanceIdent);
        if (it == mStorageStateInfos.end()) {
            return ErrorEnum::eNotFound;
        }

        it->second = storageStateInfo;

        return ErrorEnum::eNone;
    }

    bool Contains(std::function<bool(const StorageStateInstanceInfo&)> predicate)
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Check if storage state info contains";

        return std::find_if(mStorageStateInfos.begin(), mStorageStateInfos.end(), [predicate](const auto& pair) {
            return predicate(pair.second);
        }) != mStorageStateInfos.end();
    }

private:
    std::mutex                                        mMutex;
    std::map<InstanceIdent, StorageStateInstanceInfo> mStorageStateInfos;
};

template <class T>
class MessageVisitor : public StaticVisitor<bool> {
public:
    MessageVisitor(T& message, std::function<bool(const T&)> comparator)
        : mMessage(message)
        , mComparator(comparator)
    {
    }

    Res Visit(const T& variantMsg) const
    {
        if (mComparator && !mComparator(variantMsg)) {
            return false;
        }

        mMessage = variantMsg;

        return true;
    }

    template <class U>
    Res Visit(const U&) const
    {
        return false;
    }

private:
    T&                            mMessage;
    std::function<bool(const T&)> mComparator;
};

class LogVisitor : public StaticVisitor<void> {
public:
    Res Visit(const aos::cloudprotocol::StateAcceptance& state) const
    {
        LOG_DBG() << "StateAcceptance" << Log::Field("instanceIdent", state.mInstanceIdent);
    }

    Res Visit(const aos::cloudprotocol::StateRequest& state) const
    {
        LOG_DBG() << "StateRequest" << Log::Field("instanceIdent", state.mInstanceIdent);
    }

    Res Visit(const aos::cloudprotocol::NewState& state) const
    {
        LOG_DBG() << "NewState" << Log::Field("instanceIdent", state.mInstanceIdent)
                  << Log::Field("stateChecksum", state.mChecksum);
    }

    template <class T>
    Res Visit(const T& msg) const
    {
        LOG_DBG() << "Message" << Log::Field("type", typeid(msg).name());
    }
};

class CommunicationStub : public cm::communication::CommunicationItf {
public:
    Error SendMessage(const cloudprotocol::MessageVariant& body) override
    {
        std::lock_guard lock {mMutex};

        body.ApplyVisitor(LogVisitor());

        mMessages.push_back(body);
        mCondVar.notify_all();

        return ErrorEnum::eNone;
    }

    template <class T>
    Error WaitForMessage(
        const InstanceIdent& instanceIdent, T& msg, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};

        auto it = mMessages.end();

        MessageVisitor<T> visitor(
            msg, [&instanceIdent](const T& variantMsg) { return variantMsg.mInstanceIdent == instanceIdent; });

        if (!mCondVar.wait_for(lock, timeout, [this, &it, &visitor] {
                it = std::find_if(mMessages.begin(), mMessages.end(),
                    [&visitor](const auto& variant) { return variant.ApplyVisitor(visitor); });

                return it != mMessages.end();
            })) {
            return ErrorEnum::eTimeout;
        }

        mMessages.erase(it);

        return ErrorEnum::eNone;
    }

private:
    std::mutex                                 mMutex;
    std::condition_variable                    mCondVar;
    std::vector<cloudprotocol::MessageVariant> mMessages;
};

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::filesystem::path ToStatePath(std::string instanceID)
{
    return cStateDir / (instanceID.append("_state.dat"));
}

} // namespace

using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class StorageStateTests : public Test {
protected:
    void SetUp() override
    {
        std::filesystem::remove_all(cTestDir);

        std::filesystem::create_directories(cTestDir);
        std::filesystem::create_directories(cStorageDir);
        std::filesystem::create_directories(cStateDir);

        mConfig.mStorageDir = cStorageDir.c_str();
        mConfig.mStateDir   = cStateDir.c_str();

        tests::utils::InitLog();

        ASSERT_TRUE(mCryptoProvider.Init().IsNone()) << "Failed to initialize crypto provider";

        EXPECT_CALL(mFSPlatformMock, GetMountPoint)
            .WillRepeatedly(Return(RetWithError<StaticString<cFilePathLen>>(cTestDir.c_str())));
    }

    Error CalculateChecksum(const std::string& text, Array<uint8_t>& result)
    {
        auto [hasher, err] = mCryptoProvider.CreateHash(crypto::HashEnum::eSHA3_224);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        err = hasher->Update(Array<uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        err = hasher->Finalize(result);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        return ErrorEnum::eNone;
    }

    Error CalculateChecksum(const std::string& text, String& result)
    {
        StaticArray<uint8_t, crypto::cSHA2DigestSize> array;

        if (auto err = CalculateChecksum(std::string(text), array); !err.IsNone()) {
            return err;
        }

        return result.ByteArrayToHex(array);
    }

    Error AddInstanceIdent(
        const InstanceIdent& ident, const std::string& instanceID, const std::string& stateContent = "test state")
    {
        if (auto err = fs::WriteStringToFile(ToStatePath(instanceID).c_str(), stateContent.c_str(), 0600);
            !err.IsNone()) {
            return err;
        }

        auto storageItem            = std::make_unique<StorageStateInstanceInfo>();
        storageItem->mInstanceIdent = ident;
        storageItem->mStateQuota    = 2000;
        storageItem->mInstanceID    = instanceID.c_str();

        if (auto err = CalculateChecksum(stateContent, storageItem->mStateChecksum); !err.IsNone()) {
            return err;
        }

        if (auto err = mStorageStub.AddStorageStateInfo(*storageItem); !err.IsNone()) {
            return err;
        }

        return ErrorEnum::eNone;
    }

    Error FillStateAcceptance(const InstanceIdent& instanceIdent, const std::string& stateContent,
        cloudprotocol::StateResultEnum result, cloudprotocol::StateAcceptance& state)
    {
        state.mInstanceIdent = instanceIdent;
        state.mResult        = result;
        state.mReason        = cloudprotocol::StateResult(result).ToString();

        return CalculateChecksum(stateContent, state.mChecksum);
    }

    StorageStub                   mStorageStub;
    crypto::DefaultCryptoProvider mCryptoProvider;
    FSPlatformMock                mFSPlatformMock;

    CommunicationStub mCommunicationStub;
    config::Config    mConfig;
    StorageState      mStorageState;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(StorageStateTests, StartStop)
{
    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Start();
    ASSERT_TRUE(err.IsNone()) << "Failed to start storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Start();
    ASSERT_TRUE(err.Is(ErrorEnum::eWrongState)) << "Double start should fail: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Stop();
    ASSERT_TRUE(err.Is(ErrorEnum::eWrongState)) << "Double stop should fail: " << tests::utils::ErrorToStr(err);
}

TEST_F(StorageStateTests, StorageQuotaNotSet)
{
    SetupParams setupParams;
    setupParams.mInstanceIdent = {"service1", "subject1", 1};
    setupParams.mUID           = getuid();
    setupParams.mGID           = getgid();
    setupParams.mStateQuota    = 2000;
    setupParams.mStorageQuota  = 0;

    StaticString<cFilePathLen> storagePath, statePath;

    EXPECT_CALL(mFSPlatformMock, SetUserQuota(_, setupParams.mStateQuota, setupParams.mUID)).Times(1);

    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Setup(setupParams, storagePath, statePath);
    ASSERT_TRUE(err.IsNone()) << "Failed to setup storage state: " << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(mStorageStub.Contains([&setupParams](const StorageStateInstanceInfo& info) {
        return info.mInstanceIdent == setupParams.mInstanceIdent;
    })) << "Storage state info should be added";

    EXPECT_TRUE(storagePath.IsEmpty()) << "Storage path should be empty when storage quota is not set";
    EXPECT_FALSE(statePath.IsEmpty()) << "State path should not be empty when state quota is set";
}

TEST_F(StorageStateTests, StateQuotaNotSet)
{
    SetupParams setupParams;
    setupParams.mInstanceIdent = {"service1", "subject1", 1};
    setupParams.mUID           = getuid();
    setupParams.mGID           = getgid();
    setupParams.mStateQuota    = 0;
    setupParams.mStorageQuota  = 2000;

    StaticString<cFilePathLen> storagePath, statePath;

    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mFSPlatformMock, SetUserQuota(_, setupParams.mStorageQuota, setupParams.mUID)).Times(1);

    err = mStorageState.Setup(setupParams, storagePath, statePath);
    ASSERT_TRUE(err.IsNone()) << "Failed to setup storage state: " << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(mStorageStub.Contains([&setupParams](const StorageStateInstanceInfo& info) {
        return info.mInstanceIdent == setupParams.mInstanceIdent;
    })) << "Storage state info should be added";

    EXPECT_FALSE(storagePath.IsEmpty()) << "Storage path should not be empty when storage quota is set";
    EXPECT_TRUE(statePath.IsEmpty()) << "State path should be empty when state quota is not set";
}

TEST_F(StorageStateTests, StorageAndStateQuotaNotSet)
{
    SetupParams setupParams;
    setupParams.mInstanceIdent = {"service1", "subject1", 1};
    setupParams.mUID           = getuid();
    setupParams.mGID           = getgid();
    setupParams.mStateQuota    = 0;
    setupParams.mStorageQuota  = 0;

    StaticString<cFilePathLen> storagePath, statePath;

    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mFSPlatformMock, SetUserQuota).Times(0);

    err = mStorageState.Setup(setupParams, storagePath, statePath);
    ASSERT_TRUE(err.IsNone()) << "Failed to setup storage state: " << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(mStorageStub.Contains([&setupParams](const StorageStateInstanceInfo& info) {
        return info.mInstanceIdent == setupParams.mInstanceIdent;
    })) << "Storage state info should be added";

    EXPECT_TRUE(storagePath.IsEmpty()) << "Storage path should  be empty when storage quota is set";
    EXPECT_TRUE(statePath.IsEmpty()) << "State path should be empty when state quota is not set";
}

TEST_F(StorageStateTests, SetupOnDifferentPartitions)
{
    const auto cSetupParams = SetupParams {{"service1", "subject1", 1}, getuid(), getgid(), 2000, 1000};

    EXPECT_CALL(mFSPlatformMock, GetMountPoint)
        .WillOnce(Return(RetWithError<StaticString<cFilePathLen>>("partition1")))
        .WillOnce(Return(RetWithError<StaticString<cFilePathLen>>("partition2")));

    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Start();
    ASSERT_TRUE(err.IsNone()) << "Failed to start storage state: " << tests::utils::ErrorToStr(err);

    EXPECT_CALL(
        mFSPlatformMock, SetUserQuota(String(cStorageDir.c_str()), cSetupParams.mStorageQuota, cSetupParams.mUID))
        .WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(mFSPlatformMock, SetUserQuota(String(cStateDir.c_str()), cSetupParams.mStateQuota, cSetupParams.mUID))
        .WillOnce(Return(ErrorEnum::eNone));

    StaticString<cFilePathLen> storagePath, statePath;

    err = mStorageState.Setup(
        SetupParams {{"service1", "subject1", 1}, getuid(), getgid(), 2000, 1000}, storagePath, statePath);
    EXPECT_TRUE(err.IsNone()) << "Setup should succeed: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop storage state: " << tests::utils::ErrorToStr(err);
}

TEST_F(StorageStateTests, SetupFailsOnSetUserQuotaError)
{
    constexpr auto cSetQuotaError = ErrorEnum::eOutOfRange;

    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Start();
    ASSERT_TRUE(err.IsNone()) << "Failed to start storage state: " << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mFSPlatformMock, SetUserQuota).WillOnce(Return(cSetQuotaError));

    StaticString<cFilePathLen> storagePath, statePath;

    err = mStorageState.Setup(
        SetupParams {{"service1", "subject1", 1}, getuid(), getgid(), 2000, 1000}, storagePath, statePath);
    EXPECT_TRUE(err.Is(cSetQuotaError)) << "Setup should fail with SetUserQuota error: "
                                        << tests::utils::ErrorToStr(err);

    err = mStorageState.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop storage state: " << tests::utils::ErrorToStr(err);
}

TEST_F(StorageStateTests, SetupSameInstance)
{
    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Start();
    ASSERT_TRUE(err.IsNone()) << "Failed to start storage state: " << tests::utils::ErrorToStr(err);

    const struct TestParams {
        SetupParams              mSetupParams;
        std::vector<std::string> mNewStates;
        bool                     mExpectSetQuota     = false;
        bool                     mExpectNewState     = false;
        bool                     mExpectStateRequest = false;
    } params[] = {
        {{{"service1", "subject1", 1}, getuid(), getgid(), 2000, 1000}, {"state", "state 0"}, true, true},
        {{{"service1", "subject1", 1}, getuid(), getgid(), 2000, 1000}, {"state 1"}, false, true},
        {{{"service1", "subject1", 1}, getuid(), getgid(), 2000, 1000}, {"state 2"}, false, false},
        {{{"service1", "subject1", 1}, getuid(), getgid(), 2000, 2000}, {""}, true, false, true},
    };

    size_t testIndex = 0;

    for (const auto& testParam : params) {
        LOG_DBG() << "Running test case" << Log::Field("index", testIndex++);

        StaticString<cFilePathLen> storagePath, statePath;

        if (testParam.mExpectSetQuota) {
            EXPECT_CALL(mFSPlatformMock,
                SetUserQuota(_, testParam.mSetupParams.mStateQuota + testParam.mSetupParams.mStorageQuota,
                    testParam.mSetupParams.mUID))
                .Times(1);
        }

        err = mStorageState.Setup(testParam.mSetupParams, storagePath, statePath);
        ASSERT_TRUE(err.IsNone()) << "Can't setup storage state: " << tests::utils::ErrorToStr(err);

        if (testParam.mExpectStateRequest) {
            cloudprotocol::StateRequest request;

            err = mCommunicationStub.WaitForMessage(testParam.mSetupParams.mInstanceIdent, request);
            ASSERT_TRUE(err.IsNone()) << "Failed to wait for state request: " << tests::utils::ErrorToStr(err);
        }

        for (const auto& state : testParam.mNewStates) {
            std::ofstream stateFile(cStateDir / statePath.CStr());
            ASSERT_TRUE(stateFile.is_open()) << "Failed to open state file: " << (cStateDir / statePath.CStr()).c_str();

            stateFile << state << std::flush;
        }

        if (testParam.mExpectNewState) {
            cloudprotocol::NewState state;

            err = mCommunicationStub.WaitForMessage(testParam.mSetupParams.mInstanceIdent, state);
            ASSERT_TRUE(err.IsNone()) << "Failed to wait for new state: " << tests::utils::ErrorToStr(err);

            const auto expectedState = testParam.mNewStates.empty() ? std::string("") : testParam.mNewStates.back();

            EXPECT_STREQ(state.mState.CStr(), expectedState.c_str()) << "State content mismatch";

            StaticString<crypto::cSHA2DigestSize> checksumStr;

            err = CalculateChecksum(expectedState, checksumStr);
            EXPECT_TRUE(err.IsNone()) << "Failed to calculate checksum: " << tests::utils::ErrorToStr(err);

            EXPECT_EQ(state.mChecksum, checksumStr) << "Checksum mismatch";

            err = mStorageState.GetInstanceCheckSum(testParam.mSetupParams.mInstanceIdent, checksumStr);
            EXPECT_TRUE(err.IsNone()) << "Failed to get instance checksum: " << tests::utils::ErrorToStr(err);

            EXPECT_EQ(checksumStr, state.mChecksum) << "Checksum mismatch in GetInstanceCheckSum";
        }
    }

    err = mStorageState.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop storage state: " << tests::utils::ErrorToStr(err);
}

TEST_F(StorageStateTests, GetInstanceCheckSum)
{
    const auto instanceIdent = InstanceIdent {"service1", "subject1", 0};

    auto err = AddInstanceIdent(instanceIdent, "getchecksum-id", "getchecksum-content");
    ASSERT_TRUE(err.IsNone());

    err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    StaticString<crypto::cSHA2DigestSize> storedChecksumStr;

    err = mStorageState.GetInstanceCheckSum(instanceIdent, storedChecksumStr);
    ASSERT_TRUE(err.IsNone()) << "Failed to get instance checksum: " << tests::utils::ErrorToStr(err);

    err = mStorageState.GetInstanceCheckSum(InstanceIdent {"not exists", "not exists", 0}, storedChecksumStr);
    ASSERT_TRUE(err.Is(ErrorEnum::eNotFound)) << "Expected not found error, got: " << tests::utils::ErrorToStr(err);
}

TEST_F(StorageStateTests, Cleanup)
{
    const auto instanceIdent = InstanceIdent {"service1", "subject1", 0};

    auto err = AddInstanceIdent(instanceIdent, "cleanup-id", "cleanup-content");

    err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Cleanup(instanceIdent);
    ASSERT_TRUE(err.IsNone());

    err = mStorageState.Cleanup(instanceIdent);
    ASSERT_TRUE(err.Is(ErrorEnum::eNotFound));

    StorageStateInstanceInfo storageData;

    err = mStorageStub.GetStorageStateInfo(instanceIdent, storageData);
    ASSERT_TRUE(err.IsNone()) << "Failed to get storage state info: " << tests::utils::ErrorToStr(err);

    ASSERT_TRUE(std::filesystem::exists(ToStatePath(storageData.mInstanceID.CStr())))
        << "State file should exist after cleanup";
}

TEST_F(StorageStateTests, Remove)
{
    const auto instanceIdent = InstanceIdent {"service1", "subject1", 0};

    auto err = AddInstanceIdent(instanceIdent, "remove-id", "remove-content");

    err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Remove(instanceIdent);
    ASSERT_TRUE(err.IsNone());

    StorageStateInstanceInfo storageData;

    err = mStorageStub.GetStorageStateInfo(instanceIdent, storageData);
    ASSERT_TRUE(err.Is(ErrorEnum::eNotFound))
        << "Storage data should not exists after remove: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Remove(instanceIdent);
    ASSERT_TRUE(err.Is(ErrorEnum::eNotFound));
}

TEST_F(StorageStateTests, UpdateState)
{
    constexpr auto newStateContent = "updated state content";
    const auto     instanceIdent   = InstanceIdent {"service1", "subject1", 0};

    auto err = AddInstanceIdent(instanceIdent, "updatestate-id", "outdated state content");

    err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    auto updateState = std::make_unique<cloudprotocol::UpdateState>(instanceIdent);

    StaticString<crypto::cSHA2DigestSize> checksum;

    err = CalculateChecksum(newStateContent, checksum);
    ASSERT_TRUE(err.IsNone()) << "Failed to calculate checksum: " << tests::utils::ErrorToStr(err);

    ASSERT_TRUE(updateState->mState.Assign(newStateContent).IsNone());
    ASSERT_TRUE(updateState->mChecksum.Assign(checksum).IsNone());

    err = mStorageState.UpdateState(*updateState);
    ASSERT_TRUE(err.IsNone()) << "Failed to update state: " << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(mStorageStub.Contains([&instanceIdent, &checksum](const StorageStateInstanceInfo& info) {
        return info.mInstanceIdent == instanceIdent && info.mStateChecksum == checksum;
    })) << "Storage state info should be updated";

    updateState->mInstanceIdent = InstanceIdent {"not exists", "not exists", 0};

    err = mStorageState.UpdateState(*updateState);
    ASSERT_TRUE(err.Is(ErrorEnum::eNotFound));
}

TEST_F(StorageStateTests, AcceptStateUnknownInstance)
{
    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    auto acceptState = std::make_unique<cloudprotocol::StateAcceptance>(InstanceIdent {"not exists", "not exists", 0});
    acceptState->mResult = cloudprotocol::StateResultEnum::eAccepted;

    err = mStorageState.AcceptState(*acceptState);
    ASSERT_TRUE(err.Is(ErrorEnum::eNotFound));
}

TEST_F(StorageStateTests, AcceptStateChecksumMismatch)
{
    const auto cInstanceIdent = InstanceIdent {"service1", "subject1", 0};

    auto err = AddInstanceIdent(cInstanceIdent, "acceptstate-id", "initial state content");

    err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    auto acceptState       = std::make_unique<cloudprotocol::StateAcceptance>(cInstanceIdent);
    acceptState->mResult   = cloudprotocol::StateResultEnum::eAccepted;
    acceptState->mChecksum = "invalid checksum";

    err = mStorageState.AcceptState(*acceptState);
    ASSERT_TRUE(err.Is(ErrorEnum::eInvalidChecksum))
        << "Accepting state with invalid checksum should fail: " << tests::utils::ErrorToStr(err);
}

TEST_F(StorageStateTests, AcceptStateWithRejectedStatus)
{
    const auto cInstanceIdent = InstanceIdent {"service1", "subject1", 0};

    auto err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    StaticString<cFilePathLen> storagePath, statePath;

    err = mStorageState.Setup(SetupParams {cInstanceIdent, getuid(), getgid(), 2000, 1000}, storagePath, statePath);
    ASSERT_TRUE(err.IsNone()) << "Failed to setup storage state: " << tests::utils::ErrorToStr(err);

    StorageStateInstanceInfo storageData;

    err = mStorageStub.GetStorageStateInfo(cInstanceIdent, storageData);
    ASSERT_TRUE(err.IsNone()) << "Failed to get storage state info: " << tests::utils::ErrorToStr(err);

    auto acceptState       = std::make_unique<cloudprotocol::StateAcceptance>(cInstanceIdent);
    acceptState->mResult   = cloudprotocol::StateResultEnum::eRejected;
    acceptState->mChecksum = storageData.mStateChecksum;

    err = mStorageState.AcceptState(*acceptState);
    ASSERT_TRUE(err.IsNone()) << "Failed to accept state: " << tests::utils::ErrorToStr(err);

    auto stateRequest = std::make_unique<cloudprotocol::StateRequest>();

    err = mCommunicationStub.WaitForMessage(cInstanceIdent, *stateRequest);
    ASSERT_TRUE(err.IsNone()) << "Failed to wait for state request: " << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(stateRequest->mInstanceIdent == cInstanceIdent) << "State request instance ident mismatch";
}

TEST_F(StorageStateTests, UpdateAndAcceptStateFlow)
{
    const auto                 cSetupParams = SetupParams {{"service1", "subject1", 1}, getuid(), getgid(), 2000, 1000};
    constexpr auto             cStateContent       = "valid state content";
    constexpr auto             cUpdateStateContent = "updated state content";
    StaticString<cFilePathLen> storagePath, statePath;
    StaticString<crypto::cSHA2DigestSize> cStateContentChecksum, cUpdateStateContentChecksum;

    auto err = CalculateChecksum(cStateContent, cStateContentChecksum);
    ASSERT_TRUE(err.IsNone()) << "Failed to calculate checksum: " << tests::utils::ErrorToStr(err);

    err = CalculateChecksum(cUpdateStateContent, cUpdateStateContentChecksum);
    ASSERT_TRUE(err.IsNone()) << "Failed to calculate checksum: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Init(mConfig, mStorageStub, mCommunicationStub, mFSPlatformMock, mCryptoProvider);
    ASSERT_TRUE(err.IsNone()) << "Failed to initialize storage state: " << tests::utils::ErrorToStr(err);

    err = mStorageState.Start();
    ASSERT_TRUE(err.IsNone()) << "Failed to start storage state: " << tests::utils::ErrorToStr(err);

    EXPECT_CALL(
        mFSPlatformMock, SetUserQuota(_, cSetupParams.mStateQuota + cSetupParams.mStorageQuota, cSetupParams.mUID))
        .Times(1);

    // Setup storage state

    err = mStorageState.Setup(cSetupParams, storagePath, statePath);
    EXPECT_TRUE(err.IsNone()) << "Failed to setup storage state: " << tests::utils::ErrorToStr(err);

    auto stateRequest = std::make_unique<cloudprotocol::StateRequest>();

    err = mCommunicationStub.WaitForMessage(cSetupParams.mInstanceIdent, *stateRequest);
    EXPECT_TRUE(err.IsNone()) << "Failed to wait for state request: " << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(stateRequest->mInstanceIdent == cSetupParams.mInstanceIdent) << "State request instance ident mismatch";

    // Update state with initial content

    auto updateState       = std::make_unique<cloudprotocol::UpdateState>(cSetupParams.mInstanceIdent);
    updateState->mState    = cStateContent;
    updateState->mChecksum = cStateContentChecksum;

    err = mStorageState.UpdateState(*updateState);
    EXPECT_TRUE(err.IsNone()) << "Failed to update state: " << tests::utils::ErrorToStr(err);

    // Emulate service mutates its state file

    err = fs::WriteStringToFile((cStateDir / statePath.CStr()).c_str(), cUpdateStateContent, 0600);
    ASSERT_TRUE(err.IsNone()) << "Failed to write state file: " << tests::utils::ErrorToStr(err);

    // Expect storage state notices the new state and sends a new state notification

    auto newState = std::make_unique<cloudprotocol::NewState>();

    err = mCommunicationStub.WaitForMessage(cSetupParams.mInstanceIdent, *newState, std::chrono::seconds(10));
    EXPECT_TRUE(err.IsNone()) << "Failed to wait for new state: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(newState->mInstanceIdent, cSetupParams.mInstanceIdent) << "New state instance ident mismatch";
    EXPECT_STREQ(newState->mState.CStr(), cUpdateStateContent) << "New state content mismatch";
    EXPECT_EQ(newState->mChecksum, cUpdateStateContentChecksum)
        << "New state checksum mismatch: " << newState->mChecksum.CStr();

    // New state is accepted

    auto acceptState = std::make_unique<cloudprotocol::StateAcceptance>(cSetupParams.mInstanceIdent);

    err = FillStateAcceptance(
        cSetupParams.mInstanceIdent, cUpdateStateContent, cloudprotocol::StateResultEnum::eAccepted, *acceptState);
    EXPECT_TRUE(err.IsNone()) << "Failed to fill state acceptance: " << tests::utils::ErrorToStr(err);

    err = mStorageState.AcceptState(*acceptState);
    EXPECT_TRUE(err.IsNone()) << "Failed to accept state: " << tests::utils::ErrorToStr(err);

    // And the storage stub is updated

    EXPECT_TRUE(mStorageStub.Contains([&cSetupParams, &cUpdateStateContentChecksum](
                                          const StorageStateInstanceInfo& info) {
        return info.mInstanceIdent == cSetupParams.mInstanceIdent && info.mStateChecksum == cUpdateStateContentChecksum;
    })) << "Storage state info should be updated with new state checksum";

    err = mStorageState.Stop();
    ASSERT_TRUE(err.IsNone()) << "Failed to stop storage state: " << tests::utils::ErrorToStr(err);
}

} // namespace aos::cm::storagestate
