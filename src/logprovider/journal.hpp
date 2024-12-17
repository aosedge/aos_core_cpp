/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JOURNAL_HPP_
#define JOURNAL_HPP_

#include <aos/common/tools/time.hpp>
#include <memory>
#include <string>

/**
 * Forward declaration of systemd journal struct.
 */
struct sd_journal;

namespace aos::sm::logprovider {

/**
 * Journal entry.
 */
struct JournalEntry {
    /**
     * Real time.
     */
    Time mRealTime;

    /**
     * Monotonic time.
     */
    Time mMonotonicTime;

    /**
     * Message.
     */
    std::string mMessage;

    /**
     * Systemd unit.
     */
    std::string mSystemdUnit;

    /**
     * Systemd cgroup.
     */
    std::string mSystemdCGroup;
};

/**
 * Interface for systemd journal.
 */
class JournalItf {
public:
    /**
     * Destructor.
     */
    virtual ~JournalItf() = default;

    /**
     * Seeks to a specific realtime timestamp.
     *
     * @param time realtime timestamp.
     */
    virtual void SeekRealtime(Time time) = 0;

    /**
     * Seeks to the tail of the journal.
     */
    virtual void SeekTail() = 0;

    /**
     * Seeks to the head of the journal.
     */
    virtual void SeekHead() = 0;

    /**
     * Adds a disjunction to the journal filter.
     */
    virtual void AddDisjunction() = 0;

    /**
     * Adds a match to the journal filter.
     */
    virtual void AddMatch(const std::string& match) = 0;

    /**
     * Moves to the next journal entry.
     *
     * @return bool.
     */
    virtual bool Next() = 0;

    /**
     * Moves to the previous journal entry.
     *
     * @return bool.
     */
    virtual bool Previous() = 0;

    /**
     * Returns current journal entry.
     *
     * @return JournalEntry.
     */
    virtual JournalEntry GetEntry() = 0;
};

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
     */
    void AddMatch(const std::string& match) override;

    /**
     * Moves to the next journal entry.
     */
    bool Next() override;

    /**
     * Moves to the previous journal entry.
     */
    bool Previous() override;

    /**
     * Returns current journal entry.
     *
     * @return JournalEntry.
     */
    JournalEntry GetEntry() override;

private:
    sd_journal* mJournal = nullptr;
};

} // namespace aos::sm::logprovider

#endif // #define JOURNAL_HPP_
