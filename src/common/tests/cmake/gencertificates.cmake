#
# Copyright (C) 2026 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

function(gentlscertificates TARGET CERTIFICATES_DIR)
    include(${AOS_CORE_LIB_DIR}/src/core/common/tests/crypto/cmake/gencertificates.cmake)
    gencertificates(${TARGET} "${CERTIFICATES_DIR}")

    # Both identities must verify as localhost, while rotation changes the common name and key.
    file(WRITE "${CERTIFICATES_DIR}/server.ext"
         "basicConstraints=CA:FALSE\nsubjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth,clientAuth\n"
    )

    foreach(IDENTITY server rotated)
        set(COMMON_NAME "localhost")
        if(IDENTITY STREQUAL "rotated")
            set(COMMON_NAME "rotated.localhost")
        endif()

        execute_process(
            COMMAND openssl req -new -newkey rsa:2048 -nodes -keyout "${CERTIFICATES_DIR}/${IDENTITY}.key" -out
                    "${CERTIFICATES_DIR}/${IDENTITY}.csr" -subj "/CN=${COMMON_NAME}" COMMAND_ERROR_IS_FATAL ANY
        )
        execute_process(
            COMMAND
                openssl x509 -req -days 365 -in "${CERTIFICATES_DIR}/${IDENTITY}.csr" -CA "${CERTIFICATES_DIR}/ca.pem"
                -CAkey "${CERTIFICATES_DIR}/ca.key" -CAcreateserial -out "${CERTIFICATES_DIR}/${IDENTITY}.cer" -extfile
                "${CERTIFICATES_DIR}/server.ext" COMMAND_ERROR_IS_FATAL ANY
        )
    endforeach()
endfunction()
