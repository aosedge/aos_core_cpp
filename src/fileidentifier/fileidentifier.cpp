/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include "fileidentifier.hpp"
#include "logger/logmodule.hpp"

namespace aos::iam::fileidentifier {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FileIdentifier::Init(const config::Identifier& config, identhandler::SubjectsObserverItf& subjectsObserver)
{
    Error err;

    Tie(mConfig, err) = config::ParseFileIdentifierModuleParams(config.mParams);
    if (!err.IsNone()) {
        return err;
    }

    mSubjectsObserver = &subjectsObserver;

    err = FS::ReadFileToString(mConfig.mSystemIDPath.c_str(), mSystemId);
    if (!err.IsNone()) {
        return err;
    }

    err = FS::ReadFileToString(mConfig.mUnitModelPath.c_str(), mUnitModel);
    if (!err.IsNone()) {
        return err;
    }

    err = ReadSubjectsFromFile();
    if (!err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

RetWithError<StaticString<cSystemIDLen>> FileIdentifier::GetSystemID()
{
    return {mSystemId};
}

RetWithError<StaticString<cUnitModelLen>> FileIdentifier::GetUnitModel()
{
    return {mUnitModel};
}

Error FileIdentifier::GetSubjects(Array<StaticString<cSubjectIDLen>>& subjects)
{
    if (auto err = subjects.Assign(mSubjects); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error FileIdentifier::ReadSubjectsFromFile()
{
    std::ifstream file(mConfig.mSubjectsPath);
    if (!file.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eRuntime, "file not found"));
    }

    std::string subject;

    while (std::getline(file, subject)) {
        if (auto err = mSubjects.EmplaceBack(); !err.IsNone()) {
            return err;
        }

        if (auto err = mSubjects.Back().Assign(subject.c_str()); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

} // namespace aos::iam::fileidentifier
