/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <utils/exception.hpp>

#include "fileidentifier.hpp"
#include "logger/logmodule.hpp"

namespace aos::iam::fileidentifier {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FileIdentifier::Init(const config::Identifier& config, identhandler::SubjectsObserverItf& subjectsObserver)
{
    LOG_DBG() << "Initialize file identifier";

    try {
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

        ReadSubjectsFromFile();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

RetWithError<StaticString<cSystemIDLen>> FileIdentifier::GetSystemID()
{
    LOG_DBG() << "Get system ID: id=" << mSystemId.CStr();

    return {mSystemId};
}

RetWithError<StaticString<cUnitModelLen>> FileIdentifier::GetUnitModel()
{
    LOG_DBG() << "Get unit model: model=" << mUnitModel.CStr();

    return {mUnitModel};
}

Error FileIdentifier::GetSubjects(Array<StaticString<cSubjectIDLen>>& subjects)
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
        AOS_ERROR_CHECK_AND_THROW("can't set subject", err);

        err = mSubjects.Back().Assign(subject.c_str());
        AOS_ERROR_CHECK_AND_THROW("can't set subject", err);

        LOG_DBG() << "Read subject: subject=" << mSubjects.Back();
    }
}

} // namespace aos::iam::fileidentifier
