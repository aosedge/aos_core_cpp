/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <iostream>

#include <Poco/Util/ServerApplication.h>

#include "app/app.hpp"

/***********************************************************************************************************************
 * Main
 **********************************************************************************************************************/

POCO_SERVER_MAIN(aos::iam::app::App);
