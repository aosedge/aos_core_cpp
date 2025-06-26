/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include <cm/uidgidpool/uidgidpool.hpp>

using namespace testing;

namespace aos::cm::uidgidpool {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class UIDGIDPoolTest : public Test {
public:
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(UIDGIDPoolTest, uidPool)
{
    UIDPool uidPool;

    auto [uid, err] = uidPool.GetFreeID();
    ASSERT_EQ(err, ErrorEnum::eNone);
    ASSERT_EQ(uid, 5000);

    err = uidPool.LockID(5001);
    ASSERT_EQ(err, ErrorEnum::eNone);

    err = uidPool.LockID(5001);
    ASSERT_EQ(err, ErrorEnum::eFailed);

    err = uidPool.LockID(0);
    ASSERT_EQ(err, ErrorEnum::eOutOfRange);

    err = uidPool.LockID(std::numeric_limits<size_t>::max());
    ASSERT_EQ(err, ErrorEnum::eOutOfRange);

    Tie(uid, err) = uidPool.GetFreeID();
    ASSERT_EQ(err, ErrorEnum::eNone);
    ASSERT_EQ(uid, 5002);

    err = uidPool.ReleaseID(5001);
    ASSERT_EQ(err, ErrorEnum::eNone);

    Tie(uid, err) = uidPool.GetFreeID();
    ASSERT_EQ(err, ErrorEnum::eNone);
    ASSERT_EQ(uid, 5001);
}

TEST_F(UIDGIDPoolTest, gidPool)
{
    GIDPool gidPool;

    auto [uid, err] = gidPool.GetFreeID();
    ASSERT_EQ(err, ErrorEnum::eNone);
    ASSERT_EQ(uid, 5000);

    err = gidPool.LockID(5001);
    ASSERT_EQ(err, ErrorEnum::eNone);

    err = gidPool.LockID(5001);
    ASSERT_EQ(err, ErrorEnum::eFailed);

    err = gidPool.LockID(0);
    ASSERT_EQ(err, ErrorEnum::eOutOfRange);

    err = gidPool.LockID(std::numeric_limits<size_t>::max());
    ASSERT_EQ(err, ErrorEnum::eOutOfRange);

    Tie(uid, err) = gidPool.GetFreeID();
    ASSERT_EQ(err, ErrorEnum::eNone);
    ASSERT_EQ(uid, 5002);

    err = gidPool.ReleaseID(5001);
    ASSERT_EQ(err, ErrorEnum::eNone);

    Tie(uid, err) = gidPool.GetFreeID();
    ASSERT_EQ(err, ErrorEnum::eNone);
    ASSERT_EQ(uid, 5001);
}

} // namespace aos::cm::uidgidpool
