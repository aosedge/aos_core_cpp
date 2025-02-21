/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <alerts/journalalerts.hpp>
#include <aos/test/log.hpp>

#include "mocks/alertsmock.hpp"
#include "mocks/journalmock.hpp"

using namespace testing;

namespace aos {

template <typename AlertType>
struct CheckAlertEqual : StaticVisitor<bool> {
    CheckAlertEqual(const AlertType& val)
        : mVal(val)
    {
    }

    bool Visit(const AlertType& src) const { return src == mVal; }

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
 * Suite
 **********************************************************************************************************************/

class JournalAlertsTest : public Test {
public:
    void SetUp() override
    {
        aos::test::InitLog();

        mConfig
            = config::JournalAlertsConfig {{"50-udev-default.rules", "getty@tty1.service", "quotaon.service"}, 4, 4};
    }

    void Init(const std::string& cursor = "cursor");
    void SetupJournal(const std::string& cursor = "cursor");

    void Stop();

    config::JournalAlertsConfig mConfig;
    InstanceInfoProviderMock    mInstanceInfoProvider;
    aos::alerts::SenderMock     mSender;
    StorageMock                 mStorage;

    TestJournalAlerts mJournalAlerts;
};

void JournalAlertsTest::Init(const std::string& cursor)
{
    SetupJournal(cursor);

    ASSERT_TRUE(mJournalAlerts.Init(mConfig, mInstanceInfoProvider, mStorage, mSender).IsNone());
}

void JournalAlertsTest::SetupJournal(const std::string& cursor)
{
    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch(StartsWith("PRIORITY="))).Times(mConfig.mSystemAlertPriority + 1);
    EXPECT_CALL(mJournalAlerts.mJournal, AddDisjunction());
    EXPECT_CALL(mJournalAlerts.mJournal, AddMatch("_SYSTEMD_UNIT=init.scope"));
    EXPECT_CALL(mJournalAlerts.mJournal, SeekTail());
    EXPECT_CALL(mJournalAlerts.mJournal, Previous());

    EXPECT_CALL(mStorage, GetJournalCursor(_)).WillOnce(DoAll(SetArgReferee<0>(cursor.c_str()), Return(Error())));

    if (!cursor.empty()) {
        EXPECT_CALL(mJournalAlerts.mJournal, SeekCursor(cursor.c_str())).RetiresOnSaturation();
        EXPECT_CALL(mJournalAlerts.mJournal, Next());
    }
}

void JournalAlertsTest::Stop()
{
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillOnce(Return("cursor"));
    EXPECT_CALL(mStorage, SetJournalCursor(String("cursor")));

    EXPECT_TRUE(mJournalAlerts.Stop().IsNone());
}

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(JournalAlertsTest, SetupJournal)
{
    Init();
    mJournalAlerts.Start();
    Stop();
}

TEST_F(JournalAlertsTest, FailSaveCursor)
{
    Init();
    mJournalAlerts.Start();

    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillOnce(Return("cursor"));
    EXPECT_CALL(mStorage, SetJournalCursor(String("cursor"))).WillOnce(Return(Error(ErrorEnum::eFailed)));

    EXPECT_FALSE(mJournalAlerts.Stop().IsNone());
}

TEST_F(JournalAlertsTest, SendServiceAlert)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};

    entry.mSystemdUnit = "/system.slice/system-aos@service.slice/aos-service@service0.service";
    entry.mMessage     = "Hello World";

    ServiceInstanceData serviceInfo = {InstanceIdent {"service0", "service0", 0}, "0.0.0"};

    cloudprotocol::ServiceInstanceAlert alert;

    alert.mInstanceIdent  = serviceInfo.mInstanceIdent;
    alert.mServiceVersion = serviceInfo.mVersion;
    alert.mMessage        = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mInstanceInfoProvider, GetInstanceInfoByID(String("service0"))).WillOnce(Return(serviceInfo));

    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)));

    mJournalAlerts.Start();

    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, SendCoreAlert)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry      entry = {};
    cloudprotocol::CoreAlert alert;

    entry.mSystemdUnit = "aos-updatemanager.service";
    entry.mMessage     = "Hello World";

    alert.mCoreComponent = cloudprotocol::CoreComponentEnum::eUpdateManager;
    alert.mMessage       = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)));

    mJournalAlerts.Start();

    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, SendSystemAlertFiltered)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry entry = {};

    entry.mSystemdUnit = "init.service";
    entry.mMessage     = "getty@tty1.service started";

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(_)).Times(0);

    mJournalAlerts.Start();

    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, SendSystemAlert)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry        entry = {};
    cloudprotocol::SystemAlert alert;

    entry.mSystemdUnit = "init.service";
    entry.mMessage     = "Hello World";

    alert.mMessage = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)));

    mJournalAlerts.Start();

    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, InitScopeTest)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry      entry = {};
    cloudprotocol::CoreAlert alert;

    entry.mSystemdUnit = "init.scope";
    entry.mUnit        = "aos-updatemanager.service";
    entry.mMessage     = "Hello World";

    alert.mCoreComponent = cloudprotocol::CoreComponentEnum::eUpdateManager;
    alert.mMessage       = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)));

    mJournalAlerts.Start();

    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, EmptySystemdUnit)
{
    Init();

    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor()).WillRepeatedly(Return("cursor"));

    utils::JournalEntry      entry = {};
    cloudprotocol::CoreAlert alert;

    entry.mSystemdUnit   = "";
    entry.mSystemdCGroup = "/system.slice/system-aos@service.slice/aos-updatemanager.service";
    entry.mMessage       = "Hello World";

    alert.mCoreComponent = cloudprotocol::CoreComponentEnum::eUpdateManager;
    alert.mMessage       = entry.mMessage.c_str();

    EXPECT_CALL(mJournalAlerts.mJournal, GetEntry()).WillOnce(Return(entry));
    EXPECT_CALL(mSender, SendAlert(MatchVariant(alert)));

    mJournalAlerts.Start();

    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, RecoverJournalErrorOk)
{
    Init();

    // GetCursor failed
    EXPECT_CALL(mJournalAlerts.mJournal, Next()).WillRepeatedly(Return(false));

    EXPECT_CALL(mJournalAlerts.mJournal, GetCursor())
        .WillOnce(Throw(std::runtime_error("can't get journal cursor [Bad message]")))
        .WillRepeatedly(Return(std::string("cursor")));

    // Restore journal
    EXPECT_CALL(mStorage, SetJournalCursor(String(""))).WillOnce(Return(ErrorEnum::eNone));

    SetupJournal("");

    mJournalAlerts.Start();
    sleep(2);
    Stop();
}

TEST_F(JournalAlertsTest, RecoverJournalErrorFailed)
{
    Init();

    // GetCursor failed
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

    // Start journal alerts
    mJournalAlerts.Start();
    sleep(4);
    Stop();
}

} // namespace aos::sm::alerts
