#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME pbconvert)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES common.cpp)

if(WITH_IAM_API)
    list(APPEND SOURCES iam.cpp)
endif()

if(WITH_SM_API)
    list(APPEND SOURCES sm.cpp)
endif()

# ######################################################################################################################
# Target
# ######################################################################################################################

set(TARGET ${TARGET_PREFIX}_${TARGET_NAME})

add_library(${TARGET} STATIC ${SOURCES})
add_library(${TARGET_NAMESPACE}::${TARGET_NAME} ALIAS ${TARGET})

# ######################################################################################################################
# Includes
# ######################################################################################################################

# ######################################################################################################################
# Compiler flags
# ######################################################################################################################

target_compile_options(${TARGET} PRIVATE -Wstack-usage=${AOS_STACK_USAGE} -Wno-format-truncation)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

target_link_libraries(${TARGET} PUBLIC ${TARGET_NAMESPACE}::utils aoscommon Poco::Util)

if(WITH_IAM_API)
    target_link_libraries(${TARGET} PUBLIC aoscoreapi-gen-iam)
endif()

if(WITH_SM_API)
    target_link_libraries(${TARGET} PUBLIC aoscoreapi-gen-sm)
endif()
