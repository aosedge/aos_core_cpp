/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CNI_MOCK_HPP_
#define AOS_CNI_MOCK_HPP_

#include <gmock/gmock.h>

#include <sm/networkmanager/cni.hpp>

namespace aos::sm::cni {

class MockExec : public ExecItf {
public:
    MOCK_METHOD(RetWithError<std::string>, ExecPlugin,
        (const std::string& payload, const std::string& pluginPath, const std::string& args), (const, override));
};

} // namespace aos::sm::cni

#endif // AOS_CNI_MOCK_HPP_
