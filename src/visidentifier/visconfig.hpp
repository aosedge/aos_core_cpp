/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VISCONFIG_HPP_
#define VISCONFIG_HPP_

#include <string>

#include <Poco/Dynamic/Var.h>

#include <aos/common/tools/error.hpp>

#include "utils/time.hpp"

/**
 * VIS configuration.
 */
class VISConfig {
public:
    /**
     * Creates a new object instance.
     */
    VISConfig() = default;

    /**
     * Creates a new object instance.
     *
     * @param visServer VIS Server URI.
     * @param caCertFile path to ca cert file.
     * @param webSocketTimeout web socket timeout.
     */
    VISConfig(const std::string& visServer, const std::string& caCertFile, const UtilsTime::Duration& webSocketTimeout);

    /**
     * Initializes VIS Config from params.
     *
     * @param params VIS Config params.
     * @return Error.
     */
    aos::Error Init(const Poco::Dynamic::Var& params);

    /**
     * Returns VIS server URI.
     *
     * @returns const std::string&.
     */
    const std::string& GetVISServer() const;

    /**
     * Returns ca cert file path.
     *
     * @returns const std::string&.
     */
    const std::string& GetCaCertFile() const;

    /**
     * Returns web socket timeout.
     *
     * @returns const UtilsTime::Duration&.
     */
    const UtilsTime::Duration& GetWebSocketTimeout() const;

    /**
     * Returns JSON representation of a VISConfig object.
     *
     * @returns Poco::Dynamic::Var.
     */
    Poco::Dynamic::Var ToJSON() const;

    /**
     * Returns JSON representation of a VISConfig object.
     *
     * @returns std::string.
     */
    std::string ToString() const;

private:
    static constexpr const char* cVisServerTagName        = "visServer";
    static constexpr const char* cCaCertFileTagName       = "caCertFile";
    static constexpr const char* cWebSocketTimeoutTagName = "webSocketTimeout";

    std::string         mVISServer;
    std::string         mCaCertFile;
    UtilsTime::Duration mWebSocketTimeout {std::chrono::seconds(120)};
};

#endif
