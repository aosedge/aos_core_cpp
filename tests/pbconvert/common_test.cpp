/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <aos/test/log.hpp>

#include "pbconvert/common.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void CompareTimestamps(const aos::Time& lhs, const google::protobuf::Timestamp& rhs)
{
    EXPECT_EQ(lhs.UnixTime().tv_sec, rhs.seconds());
    EXPECT_EQ(lhs.UnixTime().tv_nsec, rhs.nanos());
}

} // namespace

class PBConvertCommon : public Test {
public:
    void SetUp() override { aos::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PBConvertCommon, ConvertAosErrorToProto)
{
    aos::Error params[] = {
        {aos::ErrorEnum::eFailed, "failed error"},
        {aos::ErrorEnum::eRuntime, "runtime error"},
        {aos::ErrorEnum::eNone},
    };

    size_t iteration = 0;

    for (const auto& err : params) {
        LOG_INF() << "Test iteration: " << iteration++;

        auto result = aos::common::pbconvert::ConvertAosErrorToProto(err);

        EXPECT_EQ(result.aos_code(), static_cast<int32_t>(err.Value()));
        EXPECT_EQ(result.exit_code(), err.Errno());
        EXPECT_EQ(aos::String(result.message().c_str()), err.Message()) << err.Message();
    }
}

TEST_F(PBConvertCommon, ConvertInstanceIdentToProto)
{
    aos::InstanceIdent          param {"service-id", "subject-id", 1};
    ::common::v1::InstanceIdent result = aos::common::pbconvert::ConvertToProto(param);

    EXPECT_EQ(result.service_id(), param.mServiceID.CStr());
    EXPECT_EQ(result.subject_id(), param.mSubjectID.CStr());
    EXPECT_EQ(result.instance(), param.mInstance);
}

TEST_F(PBConvertCommon, ConvertInstanceIdentToAos)
{
    ::common::v1::InstanceIdent param;

    param.set_service_id("service-id");
    param.set_subject_id("subject-id");
    param.set_instance(1);

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mServiceID, aos::String(param.service_id().c_str()));
    EXPECT_EQ(result.mSubjectID, aos::String(param.subject_id().c_str()));
    EXPECT_EQ(result.mInstance, param.instance());
}

TEST_F(PBConvertCommon, ConvertTimestampToAos)
{
    aos::Optional<aos::Time> expected {aos::Time::Now()};

    google::protobuf::Timestamp param;
    param.set_seconds(expected.GetValue().UnixTime().tv_sec);
    param.set_nanos(expected.GetValue().UnixTime().tv_nsec);

    auto result = aos::common::pbconvert::ConvertToAos(param);
    EXPECT_EQ(result, expected);

    param.Clear();
    expected.Reset();

    result = aos::common::pbconvert::ConvertToAos(param);
    EXPECT_EQ(result, expected);
}

TEST_F(PBConvertCommon, ConvertTimestampToPB)
{
    const auto time = aos::Time::Now();

    auto result = aos::common::pbconvert::TimestampToPB(time);

    CompareTimestamps(time, result);
}
