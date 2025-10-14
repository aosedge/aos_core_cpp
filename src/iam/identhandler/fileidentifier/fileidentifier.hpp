/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_FILEIDENTIFIER_FILEIDENTIFIER_HPP_
#define AOS_IAM_FILEIDENTIFIER_FILEIDENTIFIER_HPP_

#include <string>

#include <core/iam/identhandler/identmodule.hpp>

#include <iam/config/config.hpp>

namespace aos::iam::fileidentifier {

/**
 * File Identifier.
 */
class FileIdentifier : public identhandler::IdentModuleItf {
public:
    /**
     * Creates a new object instance.
     */
    FileIdentifier() = default;

    /**
     * Initializes file identifier.
     *
     * @param config config object.
     * @return Error.
     */
    Error Init(const config::IdentifierConfig& config);

    /**
     * Starts file identifier.
     *
     * @return Error.
     */
    Error Start() override { return ErrorEnum::eNone; };

    /**
     * Stops file identifier.
     *
     * @return Error.
     */
    Error Stop() override { return ErrorEnum::eNone; };

    /**
     * Returns System ID.
     *
     * @returns RetWithError<StaticString>.
     */
    RetWithError<StaticString<cIDLen>> GetSystemID() override;

    /**
     * Returns unit model.
     *
     * @returns RetWithError<StaticString>.
     */
    RetWithError<StaticString<cUnitModelLen>> GetUnitModel() override;

    /**
     * Returns subjects.
     *
     * @param[out] subjects result subjects.
     * @returns Error.
     */
    Error GetSubjects(Array<StaticString<cIDLen>>& subjects) override;

    /**
     * Subscribes subjects listener.
     *
     * @param subjectsListener subjects listener.
     * @returns Error.
     */
    Error SubscribeListener(iamclient::SubjectsListenerItf& subjectsListener) override
    {
        if (mSubjectsListener != nullptr) {
            return ErrorEnum::eAlreadyExist;
        }

        mSubjectsListener = &subjectsListener;

        return ErrorEnum::eNone;
    }

    /**
     * Unsubscribes subjects listener.
     *
     * @param subjectsListener subjects listener.
     * @returns Error.
     */
    Error UnsubscribeListener(iamclient::SubjectsListenerItf& subjectsListener) override
    {
        if (mSubjectsListener != &subjectsListener) {
            return ErrorEnum::eNotFound;
        }

        mSubjectsListener = nullptr;

        return ErrorEnum::eNone;
    }

    /**
     * Destroys object instance.
     */
    ~FileIdentifier() override = default;

private:
    void  ReadSubjectsFromFile();
    Error ReadLineFromFile(const std::string& path, String& result) const;

    config::FileIdentifierModuleParams                 mConfig;
    iamclient::SubjectsListenerItf*                    mSubjectsListener = nullptr;
    StaticString<cIDLen>                               mSystemId;
    StaticString<cUnitModelLen>                        mUnitModel;
    StaticArray<StaticString<cIDLen>, cMaxNumSubjects> mSubjects;
};

} // namespace aos::iam::fileidentifier

#endif
