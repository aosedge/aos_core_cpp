#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET aospbconvert)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES common.cpp)

if(WITH_SM_API)
    list(APPEND SOURCES sm.cpp)
endif()

# ######################################################################################################################
# Target
# ######################################################################################################################

add_library(${TARGET} STATIC ${SOURCES})

# ######################################################################################################################
# Libraries
# ######################################################################################################################

target_link_libraries(${TARGET} PUBLIC aoscommon aosutils Poco::Util)

if(WITH_IAM_API)
    target_link_libraries(${TARGET} PUBLIC aoscoreapi-gen-iam)
endif()

if(WITH_SM_API)
    target_link_libraries(${TARGET} PUBLIC aoscoreapi-gen-sm)
endif()

target_include_directories(${TARGET} PUBLIC ${AOS_CORE_COMMON_LIB_DIR}/include)

# ######################################################################################################################
# Install
# ######################################################################################################################

install(TARGETS ${TARGET} DESTINATION lib)

install(
    DIRECTORY ${AOS_CORE_COMMON_LIB_DIR}/include/pbconvert
    DESTINATION include
    FILES_MATCHING
    PATTERN "*.hpp"
)
