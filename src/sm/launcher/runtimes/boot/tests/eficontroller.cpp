/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <vector>

extern "C" {
#include <efivar/efiboot.h>
#include <efivar/efivar.h>
}

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtimes/rootfs/rootfs.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

#include <sm/launcher/runtimes/boot/eficontroller.hpp>
#include <sm/launcher/runtimes/boot/partitionmanager.hpp>

#include "partitionmanagermock.hpp"

using namespace testing;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class MockEFIVar : public EFIVarItf {
public:
    MOCK_METHOD(Error, ReadVariable, (const std::string& name, std::vector<uint8_t>& data, uint32_t& attributes),
        (const, override));
    MOCK_METHOD(Error, WriteGlobalGuidVariable,
        (const std::string& name, const std::vector<uint8_t>& data, uint32_t attributes, mode_t mode), (override));
    MOCK_METHOD(RetWithError<std::string>, GetPartUUID, (const std::string& efiVarName), (const, override));
    MOCK_METHOD(RetWithError<std::vector<std::string>>, GetAllVariables, (), (const, override));
    MOCK_METHOD(Error, CreateBootEntry,
        (const std::string& parentDevice, int partition, const std::string& loaderPath, uint16_t bootID), (override));
};

class TestEFIBootController : public EFIBootController {
public:
    TestEFIBootController(
        std::shared_ptr<MockEFIVar> efiVar, std::shared_ptr<PartitionManagerMock> partitionManagerMock)
        : mEFIVar(efiVar)
        , mPartitionManagerMock(partitionManagerMock)
    {
    }

private:
    std::shared_ptr<EFIVarItf>           CreateEFIVar() const override { return mEFIVar; }
    std::shared_ptr<PartitionManagerItf> CreatePartitionManager() const override { return mPartitionManagerMock; }

    std::shared_ptr<MockEFIVar>           mEFIVar;
    std::shared_ptr<PartitionManagerMock> mPartitionManagerMock;
};

struct TestParams {
    std::string           mVarName;
    std::optional<size_t> mBootID;
};

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class EFIBootControllerTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        mBootConfig.mDetectMode = BootDetectModeEnum::eNone;
        mBootConfig.mPartitions = {"/dev/sda1", "/dev/sda2"};
    }

    void TearDown() override { }

    void SetGetPartitionInfoExpectation(const std::vector<std::string>& efiVars = {"Boot000A", "Boot000B"})
    {
        EXPECT_CALL(*mEFIVar, GetAllVariables).WillOnce(Return(efiVars));

        EXPECT_CALL(*mMockPartitionManager, GetPartInfo("/dev/sda1", _))
            .WillOnce(Invoke([&](const std::string&, PartInfo& partInfo) {
                partInfo.mPartUUID        = "Boot000A-UUID";
                partInfo.mParentDevice    = "/dev/sda";
                partInfo.mPartitionNumber = 1;

                return ErrorEnum::eNone;
            }));
        EXPECT_CALL(*mEFIVar, GetPartUUID("Boot000A")).WillOnce(Return(RetWithError<std::string> {"Boot000A-UUID"}));

        EXPECT_CALL(*mMockPartitionManager, GetPartInfo("/dev/sda2", _))
            .WillOnce(Invoke([&](const std::string&, PartInfo& partInfo) {
                partInfo.mPartUUID        = "Boot000B-UUID";
                partInfo.mParentDevice    = "/dev/sda";
                partInfo.mPartitionNumber = 2;

                return ErrorEnum::eNone;
            }));
        EXPECT_CALL(*mEFIVar, GetPartUUID("Boot000B")).WillOnce(Return(RetWithError<std::string> {"Boot000B-UUID"}));
    }

    static constexpr auto cExpectedWriteMode = 0600;
    static constexpr auto cExpectedWriteAttributes
        = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

    BootConfig                            mBootConfig;
    std::shared_ptr<MockEFIVar>           mEFIVar               = std::make_shared<StrictMock<MockEFIVar>>();
    std::shared_ptr<PartitionManagerMock> mMockPartitionManager = std::make_shared<StrictMock<PartitionManagerMock>>();
    TestEFIBootController                 mBootController {mEFIVar, mMockPartitionManager};

    const std::vector<std::string> cExpectedDevices = {"/dev/sda1", "/dev/sda2"};
    const std::vector<TestParams>  cBootVars        = {
        {"Boot0001", 1},
        {"Boot0002", 2},
        {"Boot000A", 10},
        {"NotABootVar", std::nullopt},
        {"BootZZZZ", std::nullopt},
    };
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(EFIBootControllerTest, GetPartitionDevices)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<std::string> devices;

    err = mBootController.GetPartitionDevices(devices);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(cExpectedDevices, devices);
}

TEST_F(EFIBootControllerTest, GetCurrentBoot)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(*mEFIVar, ReadVariable("BootCurrent", _, _))
        .WillOnce(Invoke([](const std::string&, std::vector<uint8_t>& data, uint32_t& attributes) {
            data       = {0x0B, 0x00};
            attributes = 0;

            return ErrorEnum::eNone;
        }));

    size_t currentBoot = 0;

    Tie(currentBoot, err) = mBootController.GetCurrentBoot();
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(1, currentBoot);
}

TEST_F(EFIBootControllerTest, EFIBootEntriesAreCreatedOnInit)
{
    EXPECT_CALL(*mEFIVar, GetAllVariables).WillOnce(Return(std::vector<std::string> {"Boot0009"}));

    EXPECT_CALL(*mMockPartitionManager, GetPartInfo("/dev/sda1", _))
        .WillOnce(Invoke([&](const std::string&, PartInfo& partInfo) {
            partInfo.mPartUUID        = "Boot000A-UUID";
            partInfo.mParentDevice    = "/dev/sda";
            partInfo.mPartitionNumber = 1;

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mEFIVar, GetPartUUID("Boot0009")).WillOnce(Return(RetWithError<std::string> {"Boot0009-UUID"}));

    EXPECT_CALL(*mMockPartitionManager, GetPartInfo("/dev/sda2", _))
        .WillOnce(Invoke([&](const std::string&, PartInfo& partInfo) {
            partInfo.mPartUUID        = "Boot000B-UUID";
            partInfo.mParentDevice    = "/dev/sda";
            partInfo.mPartitionNumber = 2;

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mEFIVar, CreateBootEntry(_, 1, "/EFI/BOOT/bootx64.efi", 10)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mEFIVar, CreateBootEntry(_, 2, "/EFI/BOOT/bootx64.efi", 11)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mEFIVar, ReadVariable("BootOrder", _, _))
        .WillOnce(Invoke([](const std::string&, std::vector<uint8_t>& data, uint32_t& attributes) {
            data       = {0x09, 0x00}; // Boot Order: 9
            attributes = 0;
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(
        *mEFIVar, WriteGlobalGuidVariable("BootOrder", std::vector<uint8_t> {0x0A, 0x00, 0x0B, 0x00, 0x09, 0x00}, _, _))
        .WillOnce(Return(ErrorEnum::eNone));

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(EFIBootControllerTest, GetMainBootReturnsErrorIfFirstBootEntryIsUnknown)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(*mEFIVar, ReadVariable("BootOrder", _, _))
        .WillOnce(Invoke([](const std::string&, std::vector<uint8_t>& data, uint32_t& attributes) {
            data       = {0x01, 0x00, 0x0A, 0x00, 0x0B, 0x00}; // Boot Order: 1, 10, 11
            attributes = 0;

            return ErrorEnum::eNone;
        }));

    size_t mainBoot = 0;

    Tie(mainBoot, err) = mBootController.GetMainBoot();
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << tests::utils::ErrorToStr(err);
}

TEST_F(EFIBootControllerTest, GetMainBoot)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(*mEFIVar, ReadVariable("BootOrder", _, _))
        .WillOnce(Invoke([](const std::string&, std::vector<uint8_t>& data, uint32_t& attributes) {
            data       = {0x0B, 0x00, 0x0A, 0x00, 0x01, 0x00}; // Boot Order: 11, 10, 1
            attributes = 0;

            return ErrorEnum::eNone;
        }));

    size_t mainBoot = 0;

    Tie(mainBoot, err) = mBootController.GetMainBoot();
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(1, mainBoot);
}

TEST_F(EFIBootControllerTest, SetMainBootReturnsErrorOnInvalidIndex)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mBootController.SetMainBoot(111);
    EXPECT_TRUE(err.Is(ErrorEnum::eOutOfRange)) << tests::utils::ErrorToStr(err);
}

TEST_F(EFIBootControllerTest, SetMainBoot)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    const uint16_t bootID = 11;

    EXPECT_CALL(*mEFIVar,
        WriteGlobalGuidVariable("BootNext",
            std::vector<uint8_t> {
                reinterpret_cast<const uint8_t*>(&bootID), reinterpret_cast<const uint8_t*>(&bootID) + sizeof(bootID)},
            cExpectedWriteAttributes, cExpectedWriteMode))
        .WillOnce(Return(ErrorEnum::eNone));

    err = mBootController.SetMainBoot(1);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(EFIBootControllerTest, SetMainBootInvalidIndex)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mBootController.SetMainBoot(100);
    EXPECT_TRUE(err.Is(ErrorEnum::eOutOfRange)) << tests::utils::ErrorToStr(err);
}

TEST_F(EFIBootControllerTest, SetBootOK)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(*mEFIVar, ReadVariable("BootOrder", _, _))
        .WillOnce(Invoke([](const std::string&, std::vector<uint8_t>& data, uint32_t& attributes) {
            data       = {0x01, 0x00, 0x0A, 0x00, 0x02, 0x00}; // Boot Order: 1, 10, 2
            attributes = 0;
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mEFIVar, ReadVariable("BootCurrent", _, _))
        .WillOnce(Invoke([](const std::string&, std::vector<uint8_t>& data, uint32_t& attributes) {
            data       = {0x0A, 0x00}; // Boot ID 10
            attributes = 0;
            return ErrorEnum::eNone;
        }));

    std::vector<uint16_t> uint16Order {10, 1, 2};
    std::vector<uint8_t>  newBootOrder {reinterpret_cast<uint8_t*>(uint16Order.data()),
        reinterpret_cast<uint8_t*>(uint16Order.data()) + sizeof(uint16_t) * uint16Order.size()};

    EXPECT_CALL(
        *mEFIVar, WriteGlobalGuidVariable("BootOrder", newBootOrder, cExpectedWriteAttributes, cExpectedWriteMode))
        .WillOnce(Return(ErrorEnum::eNone));

    err = mBootController.SetBootOK();
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(EFIBootControllerTest, SetBootOKAlreadyHasCorrectOrder)
{
    SetGetPartitionInfoExpectation();

    auto err = mBootController.Init(mBootConfig);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(*mEFIVar, ReadVariable("BootOrder", _, _))
        .WillOnce(Invoke([](const std::string&, std::vector<uint8_t>& data, uint32_t& attributes) {
            data       = {0x01, 0x00, 0x0A, 0x00, 0x02, 0x00}; // Boot Order: 1, 10, 2
            attributes = 0;
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*mEFIVar, ReadVariable("BootCurrent", _, _))
        .WillOnce(Invoke([](const std::string&, std::vector<uint8_t>& data, uint32_t& attributes) {
            data       = {0x01, 0x00}; // Boot ID 1
            attributes = 0;
            return ErrorEnum::eNone;
        }));

    std::vector<uint8_t> newBootOrder;

    EXPECT_CALL(*mEFIVar, WriteGlobalGuidVariable).Times(0);

    err = mBootController.SetBootOK();
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

// TEST_F(EFIBootControllerTest, EFI)
// {
//     BootConfig config;

//     config.mDetectMode = BootDetectModeEnum::eNone;
//     config.mPartitions.emplace_back("/dev/nvme1n1p1");

//     EFIBootController efiController;

//     auto err = efiController.Init(config);
//     ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

//     std::vector<std::string> devices;
//     err = efiController.GetPartitionDevices(devices);
//     ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

//     ASSERT_EQ(devices.size(), 1u);
//     EXPECT_EQ(devices[0], "/dev/nvme1n1p1");

//     size_t index    = 0;
//     Tie(index, err) = efiController.GetMainBoot();

//     ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
//     EXPECT_EQ(index, 0u);
// }

} // namespace aos::sm::launcher
