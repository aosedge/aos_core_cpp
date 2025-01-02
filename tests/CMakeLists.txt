#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Add tests
# ######################################################################################################################

add_subdirectory(iamclient)
add_subdirectory(include)
add_subdirectory(jsonprovider)
add_subdirectory(migration)
add_subdirectory(ocispec)
add_subdirectory(utils)
add_subdirectory(downloader)

if(WITH_IAM_API OR WITH_SM_API)
    add_subdirectory(pbconvert)
endif()
