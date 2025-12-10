/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_UTILS_JOURNAL_HPP_
#define AOS_SM_UTILS_JOURNAL_HPP_

#include "itf/journal.hpp"

namespace aos::sm::utils {

/**
 * Journal.
 */
class Journal : public JournalItf {
public:
    /**
     * Constructor.
     */
    Journal();

    /**
     * Destructor.
     */
    ~Journal();

    /**
     * Seeks to a specific realtime timestamp.
     *
     * @param time realtime timestamp.
     */
    void SeekRealtime(Time time) override;

    /**
     * Seeks to the tail of the journal.
     */
    void SeekTail() override;

    /**
     * Seeks to the head of the journal.
     */
    void SeekHead() override;

    /**
     * Adds a disjunction to the journal filter.
     */
    void AddDisjunction() override;

    /**
     * Adds a match to the journal filter.
     *
     * @param match journal match.
     */
    void AddMatch(const std::string& match) override;

    /**
     * Moves to the next journal entry.
     *
     * @return bool.
     */
    bool Next() override;

    /**
     * Moves to the previous journal entry.
     *
     * @return bool.
     */
    bool Previous() override;

    /**
     * Returns current journal entry.
     *
     * @return JournalEntry.
     */
    JournalEntry GetEntry() override;

    /**
     * Seek to a specific cursor in the journal.
     *
     * @param cursor journal cursor.
     */
    void SeekCursor(const std::string& cursor) override;

    /**
     * Get the current cursor position.
     *
     * @return std::string.
     */
    std::string GetCursor() override;

private:
    class sd_journal* mJournal {};
};

} // namespace aos::sm::utils

#endif // #define JOURNAL_HPP_
