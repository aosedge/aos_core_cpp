/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <systemd/sd-journal.h>
#undef LOG_ERR

#include <logger/logmodule.hpp>
#include <utils/exception.hpp>

#include <Poco/NumberParser.h>

#include "journal.hpp"

namespace aos::sm::utils {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

RetWithError<std::string> ExtractValue(void* journalData, size_t journalDataLen)
{
    if (!journalData || journalDataLen == 0) {
        return {"", AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument)};
    }

    std::string message(static_cast<const char*>(journalData), journalDataLen);

    size_t delimiterPos = message.find('=');
    if (delimiterPos == std::string::npos) {
        return {"", AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument)};
    }

    std::string value = message.substr(delimiterPos + 1);

    return {value, ErrorEnum::eNone};
}

RetWithError<std::string> ExtractJournalField(sd_journal* journal, const char* field)
{
    void*  rawField    = nullptr;
    size_t rawFieldLen = 0;

    auto ret = sd_journal_get_data(journal, field, const_cast<const void**>(&rawField), &rawFieldLen);
    if (ret < 0) {
        return {"", ret};
    }

    auto [message, err] = ExtractValue(rawField, rawFieldLen);
    if (!err.IsNone()) {
        return {"", err};
    }

    return {message, ErrorEnum::eNone};
}

uint64_t ToMicroSeconds(const Time& time)
{
    return time.UnixNano() / 1000U;
}

Time FromMicroSeconds(uint64_t usec)
{
    return Time().Add(Time::cMicroseconds * usec);
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Journal::Journal()
{
    int ret = sd_journal_open(&mJournal, SD_JOURNAL_LOCAL_ONLY);
    if (ret < 0) {
        AOS_ERROR_THROW(ret, "can't open journal");
    }
}

Journal::~Journal()
{
    sd_journal_close(mJournal);
}

void Journal::SeekRealtime(Time time)
{
    if (auto ret = sd_journal_seek_realtime_usec(mJournal, ToMicroSeconds(time)); ret < 0) {
        AOS_ERROR_THROW(ret, "can't seek journal");
    }
}

void Journal::SeekTail()
{
    if (auto ret = sd_journal_seek_tail(mJournal); ret < 0) {
        AOS_ERROR_THROW(ret, "can't seek journal");
    }
}

void Journal::SeekHead()
{
    if (auto ret = sd_journal_seek_head(mJournal); ret < 0) {
        AOS_ERROR_THROW(ret, "can't seek journal");
    }
}

void Journal::AddDisjunction()
{
    if (auto ret = sd_journal_add_disjunction(mJournal); ret < 0) {
        AOS_ERROR_THROW(ret, "can't add journal disjunction");
    }
}

void Journal::AddMatch(const std::string& match)
{
    if (auto ret = sd_journal_add_match(mJournal, match.c_str(), match.length()); ret < 0) {
        AOS_ERROR_THROW(ret, "can't add journal match");
    }
}

bool Journal::Next()
{
    if (auto ret = sd_journal_next(mJournal); ret < 0) {
        AOS_ERROR_THROW(ret, "can't get journal next");
    } else if (ret > 0) {
        return true;
    }

    return false;
}

bool Journal::Previous()
{
    if (auto ret = sd_journal_previous(mJournal); ret < 0) {
        AOS_ERROR_THROW(ret, "can't get journal previous");
    } else if (ret > 0) {
        return true;
    }

    return false;
}

JournalEntry Journal::GetEntry()
{
    Error        err, ignore;
    JournalEntry entry;

    Tie(entry.mMessage, err) = ExtractJournalField(mJournal, "MESSAGE");
    AOS_ERROR_CHECK_AND_THROW(err, "failed getting message field");

    Tie(entry.mSystemdUnit, ignore)   = ExtractJournalField(mJournal, "_SYSTEMD_UNIT");
    Tie(entry.mSystemdCGroup, ignore) = ExtractJournalField(mJournal, "_SYSTEMD_CGROUP");

    std::string priority;

    Tie(priority, err) = ExtractJournalField(mJournal, "PRIORITY");
    entry.mPriority    = err.IsNone() ? Poco::NumberParser::parse(priority) : 0;

    std::string unit;
    Tie(unit, err) = ExtractJournalField(mJournal, "UNIT");
    if (err.IsNone()) {
        entry.mUnit = unit;
    }

    uint64_t   monotonicTime = 0;
    uint64_t   realTime      = 0;
    sd_id128_t bootId;

    auto ret = sd_id128_get_boot(&bootId);
    if (ret < 0) {
        AOS_ERROR_THROW(ret, "can't get journal boot");
    }

    ret = sd_journal_get_monotonic_usec(mJournal, &monotonicTime, &bootId);
    if (ret < 0) {
        AOS_ERROR_THROW(ret, "can't get journal monotonic time");
    }

    ret = sd_journal_get_realtime_usec(mJournal, &realTime);
    if (ret < 0) {
        AOS_ERROR_THROW(ret, "can't get journal real time");
    }

    entry.mMonotonicTime = FromMicroSeconds(monotonicTime);
    entry.mRealTime      = FromMicroSeconds(realTime);

    return entry;
}

void Journal::SeekCursor(const std::string& cursor)
{
    if (auto ret = sd_journal_seek_cursor(mJournal, cursor.c_str()); ret < 0) {
        AOS_ERROR_THROW(ret, "can't seek journal");
    }
}

std::string Journal::GetCursor()
{
    char* cursor = nullptr;

    if (auto ret = sd_journal_get_cursor(mJournal, &cursor); ret < 0) {
        AOS_ERROR_THROW(ret, "can't get journal cursor");
    }

    return cursor;
}

} // namespace aos::sm::utils
