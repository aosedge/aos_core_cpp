/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>

#include "fileidentifier.hpp"

namespace aos::iam::fileidentifier {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FileIdentifier::Init(const config::IdentifierConfig& config, identhandler::SubjectsObserverItf& subjectsObserver)
{
    LOG_DBG() << "Initialize file identifier";

    try {
        Error err;
        // cppcheck-suppress unusedScopedObject
        Tie(mConfig, err) = config::ParseFileIdentifierModuleParams(config.mParams);
        if (!err.IsNone()) {
            return err;
        }

        mSubjectsObserver = &subjectsObserver;

        err = ReadLineFromFile(mConfig.mSystemIDPath, mSystemId);
        AOS_ERROR_CHECK_AND_THROW(err, "can't set system id");

        err = ReadLineFromFile(mConfig.mUnitModelPath, mUnitModel);
        AOS_ERROR_CHECK_AND_THROW(err, "can't set unit model");

        ReadSubjectsFromFile();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

RetWithError<StaticString<cIDLen>> FileIdentifier::GetSystemID()
{
    LOG_DBG() << "Get system ID: id=" << mSystemId.CStr();

    return {mSystemId};
}

RetWithError<StaticString<cUnitModelLen>> FileIdentifier::GetUnitModel()
{
    LOG_DBG() << "Get unit model: model=" << mUnitModel.CStr();

    return {mUnitModel};
}

Error FileIdentifier::GetSubjects(Array<StaticString<cIDLen>>& subjects)
{
    if (auto err = subjects.Assign(mSubjects); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Get subjects: count=" << subjects.Size();

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void FileIdentifier::ReadSubjectsFromFile()
{
    std::ifstream file(mConfig.mSubjectsPath);
    if (!file.is_open()) {
        LOG_WRN() << "Can't open subjects file, empty subjects will be used";

        return;
    }

    std::string subject;

    while (std::getline(file, subject)) {
        auto err = mSubjects.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't set subject");

        err = mSubjects.Back().Assign(subject.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't set subject");

        LOG_DBG() << "Read subject: subject=" << mSubjects.Back();
    }
}

Error FileIdentifier::ReadLineFromFile(const std::string& path, String& result) const
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return ErrorEnum::eNotFound;
    }

    std::string line;

    if (!std::getline(file, line)) {
        return ErrorEnum::eFailed;
    }

    return result.Assign(line.c_str());
}

} // namespace aos::iam::fileidentifier
