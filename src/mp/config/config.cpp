/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <common/utils/json.hpp>
#include <core/common/tools/logger.hpp>

#include "config.hpp"

namespace aos::mp::config {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

constexpr auto cDefaultMaxLogPartSize  = 10 * 1024;
constexpr auto cDefaultMaxLogPartCount = 10;

Duration GetDuration(const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& key)
{
    auto value = object.GetValue<std::string>(key);

    if (value.empty()) {
        return {};
    }

    auto ret = common::utils::ParseDuration(value);
    if (!ret.mError.IsNone()) {
        throw std::runtime_error("failed to parse " + key);
    }

    return ret.mValue;
}

Download ParseDownloader(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return Download {
        object.GetValue<std::string>("DownloadDir"),
        object.GetValue<int>("MaxConcurrentDownloads"),
        GetDuration(object, "RetryDelay"),
        GetDuration(object, "MaxRetryDelay"),
    };
}

VChanConfig ParseVChanConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return VChanConfig {
        object.GetValue<int>("Domain"),
        object.GetValue<std::string>("XSRXPath"),
        object.GetValue<std::string>("XSTXPath"),
        object.GetValue<std::string>("IAMCertStorage"),
        object.GetValue<std::string>("SMCertStorage"),
    };
}

IAMConfig ParseIAMConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return IAMConfig {
        object.GetValue<std::string>("IAMPublicServerURL"),
        object.GetValue<std::string>("IAMMainPublicServerURL"),
        object.GetValue<std::string>("IAMMainProtectedServerURL"),
        object.GetValue<std::string>("CertStorage"),
        object.GetValue<int>("OpenPort"),
        object.GetValue<int>("SecurePort"),
    };
}

aos::logging::Config ParseLogProviderConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    if (!object.Has("LogProvider")) {
        return aos::logging::Config {
            cDefaultMaxLogPartSize,
            cDefaultMaxLogPartCount,
        };
    }

    auto logProviderObject = object.GetObject("LogProvider");

    return aos::logging::Config {
        logProviderObject.GetValue<uint64_t>("MaxPartSize", cDefaultMaxLogPartSize),
        logProviderObject.GetValue<uint64_t>("MaxPartCount", cDefaultMaxLogPartCount),
    };
}

CMConfig ParseCMConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return CMConfig {
        object.GetValue<std::string>("CMServerURL"),
        object.GetValue<int>("OpenPort"),
        object.GetValue<int>("SecurePort"),
    };
}

} // namespace

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

RetWithError<Config> ParseConfig(const std::string& filename)
{
    LOG_DBG() << "Parsing config file: filename=" << filename.c_str();

    std::ifstream file(filename);

    if (!file.is_open()) {
        return {Config {}, Error(ErrorEnum::eFailed, "failed to open file")};
    }

    auto result = common::utils::ParseJson(file);
    if (!result.mError.IsNone()) {
        return {Config {}, result.mError};
    }

    Config config {};

    try {
        common::utils::CaseInsensitiveObjectWrapper object(result.mValue.extract<Poco::JSON::Object::Ptr>());

        config.mWorkingDir    = object.GetValue<std::string>("WorkingDir");
        config.mVChan         = ParseVChanConfig(object.GetObject("VChan"));
        config.mCMConfig      = ParseCMConfig(object.GetObject("CMConfig"));
        config.mCertStorage   = object.GetValue<std::string>("CertStorage");
        config.mCACert        = object.GetValue<std::string>("CACert");
        config.mImageStoreDir = object.GetValue<std::string>("ImageStoreDir");
        config.mDownload      = ParseDownloader(object.GetObject("Downloader"));
        config.mIAMConfig     = ParseIAMConfig(object.GetObject("IAMConfig"));
        config.mLogConfig     = ParseLogProviderConfig(object);
    } catch (const std::exception& e) {
        return {config, Error(ErrorEnum::eFailed, e.what())};
    }

    return config;
}

} // namespace aos::mp::config
