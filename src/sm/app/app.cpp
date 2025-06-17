/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <csignal>
#include <execinfo.h>
#include <iostream>

#include <Poco/SignalHandler.h>
#include <Poco/Util/HelpFormatter.h>
#include <systemd/sd-daemon.h>

#include <aos/common/version.hpp>

#include <common/utils/exception.hpp>
#include <common/version/version.hpp>

#include "app.hpp"

namespace aos::sm::app {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void ErrorHandler(int sig)
{
    static constexpr auto cBacktraceSize = 32;

    void*  array[cBacktraceSize];
    size_t size;

    switch (sig) {
    case SIGILL:
        std::cerr << "Illegal instruction" << std::endl;
        break;

    case SIGABRT:
        std::cerr << "Aborted" << std::endl;
        break;

    case SIGFPE:
        std::cerr << "Floating point exception" << std::endl;
        break;

    case SIGSEGV:
        std::cerr << "Segmentation fault" << std::endl;
        break;

    default:
        std::cerr << "Unknown signal" << std::endl;
        break;
    }

    size = backtrace(array, cBacktraceSize);

    backtrace_symbols_fd(array, size, STDERR_FILENO);

    raise(sig);
}

void RegisterErrorSignals()
{
    struct sigaction act { };

    act.sa_handler = ErrorHandler;
    act.sa_flags   = SA_RESETHAND;

    sigaction(SIGILL, &act, nullptr);
    sigaction(SIGABRT, &act, nullptr);
    sigaction(SIGFPE, &act, nullptr);
    sigaction(SIGSEGV, &act, nullptr);
}

} // namespace

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

void App::initialize(Application& self)
{
    if (mStopProcessing) {
        return;
    }

    RegisterErrorSignals();

    Application::initialize(self);

    mAosCore = std::make_unique<AosCore>();

    mAosCore->Init(mConfigFile);

    mInitialized = true;

    mAosCore->Start();

    // Notify systemd

    auto ret = sd_notify(0, cSDNotifyReady);
    if (ret < 0) {
        AOS_ERROR_CHECK_AND_THROW(ret, "can't notify systemd");
    }
}

void App::uninitialize()
{
    Application::uninitialize();

    if (!mInitialized) {
        return;
    }

    mAosCore->Stop();
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
    options.addOption(Poco::Util::Option("version", "", "displays version information")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleVersion)));
    options.addOption(Poco::Util::Option("journal", "j", "redirects logs to systemd journal")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleJournal)));
    options.addOption(Poco::Util::Option("verbose", "v", "sets current log level")
                          .argument("${level}")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleLogLevel)));
    options.addOption(Poco::Util::Option("config", "c", "path to config file")
                          .argument("${file}")
                          .callback(Poco::Util::OptionCallback<App>(this, &App::HandleConfigFile)));
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
    helpFormatter.setHeader("Aos SM manager service.");
    helpFormatter.format(std::cout);

    stopOptionsProcessing();
}

void App::HandleVersion(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mStopProcessing = true;

    std::cout << "Aos service manager version: " << AOS_CORE_CPP_VERSION << std::endl;
    std::cout << "Aos core library version:    " << AOS_CORE_VERSION << std::endl;

    stopOptionsProcessing();
}

void App::HandleJournal(const std::string& name, const std::string& value)
{
    (void)name;
    (void)value;

    mAosCore->SetLogBackend(aos::common::logger::Logger::Backend::eJournald);
}

void App::HandleLogLevel(const std::string& name, const std::string& value)
{
    (void)name;

    aos::LogLevel level;

    auto err = level.FromString(aos::String(value.c_str()));
    if (!err.IsNone()) {
        throw Poco::Exception("unsupported log level", value);
    }

    mAosCore->SetLogLevel(level);
}

void App::HandleConfigFile(const std::string& name, const std::string& value)
{
    (void)name;

    mConfigFile = value;
}

} // namespace aos::sm::app
