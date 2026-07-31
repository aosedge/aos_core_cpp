/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_APP_APP_HPP_
#define AOS_IAM_APP_APP_HPP_

#include <Poco/Util/ServerApplication.h>

#include "aoscore.hpp"

namespace aos::iam::app {

/**
 * Aos IAM application.
 */
class App : public Poco::Util::ServerApplication {
public:
    /**
     * Constructor.
     */
    App() = default;

protected:
    void initialize(Application& self) override;
    void uninitialize() override;
    void reinitialize(Application& self) override;
    int  main(const ArgVec& args) override;
    void defineOptions(Poco::Util::OptionSet& options) override;

private:
    static constexpr auto cSDNotifyReady = "READY=1";

    void HandleHelp(const std::string& name, const std::string& value);
    void HandleVersion(const std::string& name, const std::string& value);
    void HandleProvisioning(const std::string& name, const std::string& value);
    void HandleJournal(const std::string& name, const std::string& value);
    void HandleLogLevel(const std::string& name, const std::string& value);
    void HandleConfigFile(const std::string& name, const std::string& value);

    std::unique_ptr<AosCore> mAosCore;
    common::logger::Logger   mLogger;
    bool                     mStopProcessing = false;
    bool                     mInitialized    = false;
    bool                     mProvisioning   = false;
    std::string              mConfigFile;
};

} // namespace aos::iam::app

#endif
