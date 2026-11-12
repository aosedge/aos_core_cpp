/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/alerts/journalalerts.hpp>
#include <sm/launcher/runtimes/container/itf/consts.hpp>
#include <sm/tests/mocks/journalmock.hpp>

using namespace testing;

namespace aos {

template <typename AlertType>
struct CheckAlertEqual : StaticVisitor<bool> {
    CheckAlertEqual(const AlertType& val)
        : mVal(val)
    {
    }

    bool Visit(const AlertType& src) const
    {
        auto copy       = src;
        copy.mTimestamp = mVal.mTimestamp; // ignore timestamp in comparison

        return copy == mVal;
    }

    template <typename T>
    bool Visit(const T& src) const
    {
        (void)src;

        return false;
    }

private:
    AlertType mVal;
};

MATCHER_P(MatchVariant, val, "Match variant")
{
    return arg.ApplyVisitor(CheckAlertEqual(val));
}

} // namespace aos

namespace aos::sm::alerts {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class TestJournalAlerts : public JournalAlerts {
public:
    std::shared_ptr<utils::JournalItf> CreateJournal() override
    {
        return std::shared_ptr<utils::JournalItf>(&mJournal, [](utils::JournalItf*) {});
    }

    utils::JournalMock mJournal;
};

/***********************************************************************************************************************
 * Mocks
 **********************************************************************************************************************/

class SenderMock : public aos::alerts::SenderItf {
public:
    MOCK_METHOD(Error, SendAlert, (const AlertVariant& alert), (override));
};

class StorageMock : public StorageItf {
public:
    MOCK_METHOD(Error, SetJournalCursor, (const String&), (override));
    MOCK_METHOD(Error, GetJournalCursor, (String&), (const, override));
};

class InstanceInfoProviderMock : public InstanceInfoProviderItf {
public:
    MOCK_METHOD(Error, GetInstanceInfoByID, (const String&, InstanceInfo&), (override));
};

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class JournalAlertsTest : public Test {
public:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mConfig
            = common::config::JournalAlerts {{"50-udev-default.rules", "getty@tty1.service", "quotaon.service"}, 4, 4};
    }

    void Init();
    void Stop();
    void Start();

    Error NotifyAlertSent();
    void  WaitForAlert(std::chrono::milliseconds timeout = std::chrono::seconds(2));

    std::mutex              mAlertMutex;
    std::condition_variable mAlertCV;
    bool                    mAlertSent = false;
    std::atomic_bool        mStopping  = false;

    common::config::JournalAlerts mConfig;
    InstanceInfoProviderMock      mInstanceInfoProvider;
    SenderMock                    mSender;
    StorageMock                   mStorage;
    std::string                   mCursor = "cursor";

    TestJournalAlerts mJournalAlerts;
};

void JournalAlertsTest::Init()
{
    // Expectations must not be changed while the monitor thread calls the mock, so set the default one here.
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));
    EXPECT_CALL(mStorage, SetJournalCursor(String("cursor"))).WillRepeatedly(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mJournalAlerts.Init(mConfig, &mInstanceInfoProvider, mStorage, mSender).IsNone());
}

void JournalAlertsTest::Start()
{
    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch(StartsWith("PRIORITY="))).Times(AnyNumber());
    EXPECT_CALL(mJournalAlerts.mJournal, AddDisjunction()).Times(AnyNumber());
    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch("_SYSTEMD_UNIT=init.scope")).Times(AnyNumber());
    EXPECT_CALL(mJournalAlerts.mJournal, SeekTail()).Times(AnyNumber());
    EXPECT_CALL(mJournalAlerts.mJournal, Previous()).Times(AnyNumber());

    EXPECT_CALL(mStorage, GetJournalCursor(_))
        .WillOnce(DoAll(SetArgReferee<0>(mCursor.c_str()), Return(Error())))
        .WillRepeatedly(DoAll(SetArgReferee<0>(""), Return(Error())));

    EXPECT_CALL(mJournalAlerts.mJournal, SeekCursor(mCursor.c_str())).RetiresOnSaturation();

    ASSERT_TRUE(mJournalAlerts.Start().IsNone());
}

void JournalAlertsTest::Stop()
{
    mStopping = true;

    EXPECT_TRUE(mJournalAlerts.Stop().IsNone());
}

Error JournalAlertsTest::NotifyAlertSent()
{
    std::lock_guard<std::mutex> lock(mAlertMutex);

    mAlertSent = true;
    mAlertCV.notify_one();

    return ErrorEnum::eNone;
}

void JournalAlertsTest::WaitForAlert(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mAlertMutex);

    mAlertCV.wait_for(lock, timeout, [&] { return mAlertSent; });
}

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(JournalAlertsTest, SetupJournal)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next()).Times(AnyNumber());

    Start();
    Stop();
}

TEST_F(JournalAlertsTest, FailSaveCursor)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next()).Times(AnyNumber());
    EXPECT_CALL(mStorage, SetJournalCursor(String("cursor"))).WillOnce(Return(Error(ErrorEnum::eFailed)));

    Start();

    EXPECT_FALSE(mJournalAlerts.Stop().IsNone());
}

TEST_F(JournalAlertsTest, SendInstanceAlert)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};

    entry.mSystemdCGroup = std::string(launcher::cCgroupsPath) + "/instance0";
    entry.mMessage       = "Hello World";

    InstanceInfo instanceInfo {InstanceIdent {"service0", "subject0", 0, UpdateItemTypeEnum::eService}, "1.0.0"};

    InstanceAlert alert;

    static_cast<InstanceIdent&>(alert) = instanceInfo.mInstanceIdent;
    alert.mVersion                     = instanceInfo.mVersion;
    alert.mMessage                     = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mInstanceInfoProvider, GetInstanceInfoByID(String("instance0"), _))
        .WillOnce(DoAll(SetArgReferee<1>(instanceInfo), Return(Error())));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)))
        .WillOnce(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent));

    Start();

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, SendSystemAlertForUnknownInstance)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};

    entry.mSystemdCGroup = std::string(launcher::cCgroupsPath) + "/instance0";
    entry.mMessage       = "Hello World";

    SystemAlert alert;

    alert.mMessage = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mInstanceInfoProvider, GetInstanceInfoByID(String("instance0"), _))
        .WillOnce(Return(Error(ErrorEnum::eNotFound)));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)))
        .WillOnce(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent));

    Start();

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, SendCoreAlert)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};
    CoreAlert           alert;

    entry.mSystemdUnit = "aos-cm.service";
    entry.mMessage     = "Hello World";

    alert.mCoreComponent = CoreComponentEnum::eCM;
    alert.mMessage       = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)))
        .WillOnce(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent));

    Start();

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, SendSystemAlertFiltered)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};

    entry.mSystemdUnit = "init.service";
    entry.mMessage     = "getty@tty1.service started";

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry())
        .WillOnce(DoAll(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent), Return(entry)));
    EXPECT_CALL(mSender, SendAlert(_)).Times(0);

    Start();

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, SendSystemAlert)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};
    SystemAlert         alert;

    entry.mSystemdUnit = "init.service";
    entry.mMessage     = "Hello World";

    alert.mMessage = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)))
        .WillOnce(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent));

    Start();

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, InitScopeTest)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};
    CoreAlert           alert;

    entry.mSystemdUnit = "init.scope";
    entry.mUnit        = "aos-cm.service";
    entry.mMessage     = "Hello World";

    alert.mCoreComponent = CoreComponentEnum::eCM;
    alert.mMessage       = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)))
        .WillOnce(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent));

    Start();

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, EmptySystemdUnit)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};
    CoreAlert           alert;

    entry.mSystemdUnit   = "";
    entry.mSystemdCGroup = "/system.slice/system-aos@service.slice/aos-cm.service";
    entry.mMessage       = "Hello World";

    alert.mCoreComponent = CoreComponentEnum::eCM;
    alert.mMessage       = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)))
        .WillOnce(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent));

    Start();

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, RecoverJournalErrorOk)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillRepeatedly(Return(false));

    // GetCursor failed
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor())
        .WillOnce(Throw(std::runtime_error("can't get journal cursor [Bad message]")))
        .WillRepeatedly(Return(std::string("cursor")));

    // Restore journal
    EXPECT_CALL(mStorage, SetJournalCursor(String("")))
        .WillOnce(DoAll(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent), Return(ErrorEnum::eNone)));

    Start();

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, RecoverJournalErrorFailed)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Wait(_)).WillRepeatedly(Return(std::chrono::microseconds::zero()));
    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillRepeatedly(Return(false));

    // GetCursor fails until stop is requested
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Invoke([this]() -> std::string {
        if (!mStopping) {
            throw std::runtime_error("can't get journal cursor [Bad message]");
        }

        return "cursor";
    }));

    // Restore journal
    EXPECT_CALL(mStorage, SetJournalCursor(String("")))
        .WillOnce(DoAll(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent), Return(ErrorEnum::eNone)))
        .WillRepeatedly(Return(ErrorEnum::eNone));

    Start();

    WaitForAlert();
    Stop();
}

} // namespace aos::sm::alerts
