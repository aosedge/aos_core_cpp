#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# AosCore config
# ######################################################################################################################

# Aos core configuration
add_definitions(-include ${CMAKE_CURRENT_SOURCE_DIR}/aoscoreconfig.hpp)

# ######################################################################################################################
# External targets
# ######################################################################################################################

include(AddModule)

add_subdirectory(${AOS_CORE_API_DIR} aos_core_api)
add_subdirectory(${AOS_CORE_LIB_DIR}/src/common aos_core_lib/common)

if(WITH_SM)
    add_subdirectory(${AOS_CORE_LIB_DIR}/src/sm aos_core_lib/sm)
endif()

if(WITH_MP OR WITH_IAM)
    add_subdirectory(${AOS_CORE_LIB_DIR}/src/iam aos_core_lib/iam)
endif()

if(WITH_TEST)
    add_subdirectory(${AOS_CORE_LIB_DIR}/tests/utils aos_core_lib_cpp/tests/utils)
    add_subdirectory(${AOS_CORE_LIB_DIR}/tests/include aos_core_lib_cpp/tests/include)
endif()

# ######################################################################################################################
# Target properties
# ######################################################################################################################

set(TARGET_NAMESPACE aos)
set(TARGET_PREFIX aos)
set(TARGET_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})

# ######################################################################################################################
# AosCore components
# ######################################################################################################################

add_subdirectory(common)

if(WITH_IAM)
    add_subdirectory(iam)
endif()

if(WITH_MP)
    add_subdirectory(mp)
endif()

if(WITH_CM)
    add_subdirectory(cm)
endif()

if(WITH_SM)
    add_subdirectory(sm)
endif()
