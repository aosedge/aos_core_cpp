/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JOURNAL_STUB_HPP_
#define JOURNAL_STUB_HPP_

#include <vector>

#include <sm/utils/journal.hpp>

namespace aos::sm::utils {

class JournalStub : public JournalItf {
public:
    void SeekRealtime(Time time) override
    {
        mCurrentEntry = std::find_if(mJournal.begin(), mJournal.end(),
            [&time](const JournalEntry& entry) { return entry.mRealTime.UnixNano() >= time.UnixNano(); });

        if (mCurrentEntry == mJournal.end() && !mJournal.empty()) {
            mCurrentEntry = mJournal.end() - 1;
        }
    }

    void SeekTail() override
    {
        if (!mJournal.empty()) {
            mCurrentEntry = mJournal.end() - 1;
        } else {
            mCurrentEntry = mJournal.end();
        }
    }

    void SeekHead() override { mCurrentEntry = mJournal.begin(); }

    void AddDisjunction() override { }

    void AddMatch(const std::string& match) override { (void)match; }

    bool Next() override
    {
        if (!mSearchStarted) {
            mSearchStarted = true;

            return mCurrentEntry != mJournal.end();
        }

        if (mCurrentEntry == mJournal.end() || mCurrentEntry + 1 == mJournal.end()) {
            return false;
        }

        ++mCurrentEntry;

        return true;
    }

    bool Previous() override
    {
        if (!mSearchStarted) {
            mSearchStarted = true;

            return mCurrentEntry != mJournal.end();
        }

        if (mCurrentEntry == mJournal.begin()) {
            return false;
        }

        --mCurrentEntry;

        return true;
    }

    JournalEntry GetEntry() override
    {
        if (mCurrentEntry == mJournal.end()) {
            throw std::out_of_range("No current entry in the journal.");
        }
        return *mCurrentEntry;
    }

    void AddMessage(const std::string& message, const std::string& systemdUnit, const std::string& cgroupUnit)
    {
        JournalEntry entry;

        entry.mMonotonicTime = entry.mRealTime = Time::Now();
        entry.mMessage                         = message;
        entry.mSystemdUnit                     = systemdUnit;
        entry.mSystemdCGroup                   = cgroupUnit;

        mJournal.emplace_back(entry);
    }

    void SeekCursor(const std::string& cursor) override { (void)cursor; }

    std::string GetCursor() override { return ""; }

private:
    std::vector<JournalEntry>           mJournal;
    std::vector<JournalEntry>::iterator mCurrentEntry  = mJournal.end();
    bool                                mSearchStarted = false;
};

} // namespace aos::sm::utils

#endif // JOURNAL_STUB_HPP_
