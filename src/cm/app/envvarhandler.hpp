/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_APP_ENVVARHANDLER_HPP_
#define AOS_CM_APP_ENVVARHANDLER_HPP_

#include <core/cm/launcher/itf/envvarhandler.hpp>

namespace aos::cm::launcher {

/** @addtogroup cm Communication Manager
 *  @{
 */

/**
 * Environment variable handler interface.
 */
class EnvVarHandler : public EnvVarHandlerItf {
public:
    /**
     * Overrides environment variables.
     *
     * @param envVars environment variables.
     * @return Error.
     */
    Error OverrideEnvVars(const OverrideEnvVarsRequest& envVars) override
    {
        (void)envVars;

        return ErrorEnum::eNotSupported;
    }
};

} // namespace aos::cm::launcher

#endif
