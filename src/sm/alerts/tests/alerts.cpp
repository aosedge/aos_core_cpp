/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/alerts/journalalerts.hpp>
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

    common::config::JournalAlerts mConfig;
    InstanceInfoProviderMock      mInstanceInfoProvider;
    SenderMock                    mSender;
    StorageMock                   mStorage;
    std::string                   mCursor = "cursor";

    TestJournalAlerts mJournalAlerts;
};

void JournalAlertsTest::Init()
{
    ASSERT_TRUE(mJournalAlerts.Init(mConfig, mInstanceInfoProvider, mStorage, mSender).IsNone());
}

void JournalAlertsTest::Start()
{
    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch(StartsWith("PRIORITY="))).Times(mConfig.mSystemAlertPriority + 1);
    EXPECT_CALL(mJournalAlerts.mJournal, AddDisjunction());
    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch("_SYSTEMD_UNIT=init.scope"));
    EXPECT_CALL(mJournalAlerts.mJournal, SeekTail());
    EXPECT_CALL(mJournalAlerts.mJournal, Previous());

    EXPECT_CALL(mStorage, GetJournalCursor(_)).WillOnce(DoAll(SetArgReferee<0>(mCursor.c_str()), Return(Error())));

    EXPECT_CALL(mJournalAlerts.mJournal, SeekCursor(mCursor.c_str())).RetiresOnSaturation();
    EXPECT_CALL(mJournalAlerts.mJournal, Next());

    ASSERT_TRUE(mJournalAlerts.Start().IsNone());
}

void JournalAlertsTest::Stop()
{
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));
    EXPECT_CALL(mStorage, SetJournalCursor(String("cursor")));

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
    Start();
    Stop();
}

TEST_F(JournalAlertsTest, FailSaveCursor)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillOnce(Return("cursor"));
    EXPECT_CALL(mStorage, SetJournalCursor(String("cursor"))).WillOnce(Return(Error(ErrorEnum::eFailed)));

    EXPECT_FALSE(mJournalAlerts.Stop().IsNone());
}

TEST_F(JournalAlertsTest, SendServiceAlert)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};

    entry.mSystemdUnit = "/system.slice/system-aos@service.slice/aos-service@service0.service";
    entry.mMessage     = "Hello World";

    InstanceInfo instanceInfo {InstanceIdent {"service0", "service0", 0, UpdateItemTypeEnum::eService}, "0.0.0"};

    InstanceAlert alert;

    static_cast<InstanceIdent&>(alert) = instanceInfo.mInstanceIdent;
    alert.mVersion                     = instanceInfo.mVersion;
    alert.mMessage                     = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mInstanceInfoProvider, GetInstanceInfoByID(String("service0"), _))
        .WillOnce(DoAll(SetArgReferee<1>(instanceInfo), Return(Error())));

    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)))
        .WillOnce(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent));

    WaitForAlert();

    Stop();
}

TEST_F(JournalAlertsTest, SendCoreAlert)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
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

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, SendSystemAlertFiltered)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};

    entry.mSystemdUnit = "init.service";
    entry.mMessage     = "getty@tty1.service started";

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(_)).Times(0);

    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, SendSystemAlert)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};
    SystemAlert         alert;

    entry.mSystemdUnit = "init.service";
    entry.mMessage     = "Hello World";

    alert.mMessage = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)))
        .WillOnce(InvokeWithoutArgs(this, &JournalAlertsTest::NotifyAlertSent));

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, InitScopeTest)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
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

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, EmptySystemdUnit)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
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

    WaitForAlert();
    Stop();
}

TEST_F(JournalAlertsTest, RecoverJournalErrorOk)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillRepeatedly(Return(false));

    // GetCursor failed
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor())
        .WillOnce(Throw(std::runtime_error("can't get journal cursor [Bad message]")))
        .WillRepeatedly(Return(std::string("cursor")));

    // Restore journal
    EXPECT_CALL(mStorage, SetJournalCursor(String(""))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch(StartsWith("PRIORITY="))).Times((mConfig.mSystemAlertPriority + 1));
    EXPECT_CALL(mJournalAlerts.mJournal, AddDisjunction());
    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch("_SYSTEMD_UNIT=init.scope"));
    EXPECT_CALL(mJournalAlerts.mJournal, SeekTail());
    EXPECT_CALL(mJournalAlerts.mJournal, Previous());
    EXPECT_CALL(mStorage, GetJournalCursor(_)).WillOnce(DoAll(SetArgReferee<0>(""), Return(Error())));

    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, RecoverJournalErrorFailed)
{
    Init();
    Start();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillRepeatedly(Return(false));

    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor())
        .WillRepeatedly(Throw(std::runtime_error("can't get journal cursor [Bad message]")));

    // Restore journal
    EXPECT_CALL(mStorage, SetJournalCursor(String(""))).WillRepeatedly(Return(ErrorEnum::eNone));

    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch(StartsWith("PRIORITY="))).Times(AnyNumber());
    EXPECT_CALL(mJournalAlerts.mJournal, AddDisjunction()).Times(AnyNumber());
    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch("_SYSTEMD_UNIT=init.scope")).Times(AnyNumber());
    EXPECT_CALL(mJournalAlerts.mJournal, SeekTail()).Times(AnyNumber());
    EXPECT_CALL(mJournalAlerts.mJournal, Previous()).Times(AnyNumber());
    EXPECT_CALL(mStorage, GetJournalCursor(_)).WillRepeatedly(DoAll(SetArgReferee<0>(""), Return(Error())));

    sleep(4);
    Stop();
}

} // namespace aos::sm::alerts
