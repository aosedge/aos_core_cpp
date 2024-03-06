/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <csignal>
#include <execinfo.h>
#include <iostream>

#include <Poco/SignalHandler.h>
#include <Poco/Util/HelpFormatter.h>
#include <aos/common/version.hpp>
#include <systemd/sd-daemon.h>

#include "app.hpp"
#include "log.hpp"
#include "utils/exception.hpp"
#include "version.hpp"

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static void SegmentationHandler(int sig)
{
    static constexpr auto cBacktraceSize = 32;

    void*  array[cBacktraceSize];
    size_t size;

    LOG_ERR() << "Segmentation fault";

    size = backtrace(array, cBacktraceSize);

    backtrace_symbols_fd(array, size, STDERR_FILENO);

    raise(sig);
}

static void RegisterSegfaultSignal()
{
    struct sigaction act { };

    act.sa_handler = SegmentationHandler;
    act.sa_flags   = SA_RESETHAND;

    sigaction(SIGSEGV, &act, nullptr);
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

void App::initialize(Application& self)
{
    RegisterSegfaultSignal();

    auto err = mLogger.Init();
    AOS_ERROR_CHECK_AND_THROW("can't initialize logger", err);

    Application::initialize(self);

    LOG_INF() << "Initialize IAM: version = " << AOS_CORE_IAM_VERSION;

    // Initialize Aos modules

    auto ret = sd_notify(0, cSDNotifyReady);
    AOS_ERROR_CHECK_AND_THROW("can't notify systemd", ret);
}

void App::uninitialize()
{
    Application::uninitialize();
}

void App::reinitialize(Application& self)
{
    Application::reinitialize(self);
}

int App::main(const ArgVec& args)
{
    (void)args;

    if (mStopProcessing) {
        return Application::EXIT_OK;
    }

    waitForTerminationRequest();

    return Application::EXIT_OK;
}

void App::defineOptions(Poco::Util::OptionSet& options)
{
    Application::defineOptions(options);

    options.addOption(Poco::Util::Option("help", "h", "displays help information")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleHelp)));
    options.addOption(Poco::Util::Option("version", "v", "displays version information")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleVersion)));
    options.addOption(Poco::Util::Option("provisioning", "p", "enables provisioning mode")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleProvisioning)));
    options.addOption(Poco::Util::Option("journal", "j", "redirects logs to systemd journal")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleJournal)));
    options.addOption(Poco::Util::Option("loglevel", "l", "sets current log level")
                          .argument("level")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleLogLevel)));
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void App::HandleHelp(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mStopProcessing = true;

    Poco::Util::HelpFormatter helpFormatter(options());

    helpFormatter.setCommand(commandName());
    helpFormatter.setUsage("[OPTIONS]");
    helpFormatter.setHeader("Aos IAM manager service.");
    helpFormatter.format(std::cout);

    stopOptionsProcessing();
}

void App::HandleVersion(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mStopProcessing = true;

    std::cout << "Aos IA manager version:   " << AOS_CORE_IAM_VERSION << std::endl;
    std::cout << "Aos core library version: " << AOS_CORE_VERSION << std::endl;

    stopOptionsProcessing();
}

void App::HandleProvisioning(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mProvisioning = true;
}

void App::HandleJournal(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mLogger.SetBackend(Logger::Backend::eJournald);
}

void App::HandleLogLevel(const std::string& name, const std::string& value)
{
    (void)name;

    aos::LogLevel level;

    auto err = level.FromString(aos::String(value.c_str()));
    if (!err.IsNone()) {
        throw Poco::Exception("unsupported log level", value);
    }

    mLogger.SetLogLevel(level);
}
