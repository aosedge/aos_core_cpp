/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_APP_APP_HPP_
#define AOS_SM_APP_APP_HPP_

#include <Poco/Util/ServerApplication.h>

#include "aoscore.hpp"

namespace aos::sm::app {

/**
 * Aos SM application.
 */
class App : public Poco::Util::ServerApplication {
protected:
    void initialize(Application& self);
    void uninitialize();
    void reinitialize(Application& self);
    int  main(const ArgVec& args);
    void defineOptions(Poco::Util::OptionSet& options);

private:
    static constexpr auto cSDNotifyReady = "READY=1";

    void HandleHelp(const std::string& name, const std::string& value);
    void HandleVersion(const std::string& name, const std::string& value);
    void HandleJournal(const std::string& name, const std::string& value);
    void HandleLogLevel(const std::string& name, const std::string& value);
    void HandleConfigFile(const std::string& name, const std::string& value);

    bool                     mStopProcessing = false;
    bool                     mInitialized    = false;
    std::string              mConfigFile;
    std::unique_ptr<AosCore> mAosCore;
};

}; // namespace aos::sm::app

#endif
