/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_TESTS_MOCKS_JOURNALMOCK_HPP_
#define AOS_SM_TESTS_MOCKS_JOURNALMOCK_HPP_

#include <gmock/gmock.h>
#include <vector>

#include <sm/utils/journal.hpp>

namespace aos::sm::utils {

class JournalMock : public JournalItf {
public:
    MOCK_METHOD(void, SeekRealtime, (Time time), (override));
    MOCK_METHOD(void, SeekTail, (), (override));
    MOCK_METHOD(void, SeekHead, (), (override));
    MOCK_METHOD(void, AddDisjunction, (), (override));
    MOCK_METHOD(void, AddMatch, (const std::string& match), (override));
    MOCK_METHOD(bool, Next, (), (override));
    MOCK_METHOD(bool, Previous, (), (override));
    MOCK_METHOD(JournalEntry, GetEntry, (), (override));
    MOCK_METHOD(void, SeekCursor, (const std::string& cursor), (override));
    MOCK_METHOD(std::string, GetCursor, (), (override));
};

} // namespace aos::sm::utils

#endif // JOURNAL_MOCK_HPP_
