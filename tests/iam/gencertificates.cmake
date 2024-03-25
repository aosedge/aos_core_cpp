#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

#
# GenCertificates function generates:
#  CA artifacts: ca.pem & ca.key in PEM format, ca.cer.der in DER format
#  Client artifacts: client.cer & client.key in PEM, client.cer.der in DER format
#  Client-CA certificate chain: client-ca-chain.pem
#
function(gencertificates TARGET CERTIFICATES_DIR)
    file(MAKE_DIRECTORY ${CERTIFICATES_DIR})

    message("\nGenerating PKCS11 test certificates...")

    message("\nCreate a Certificate Authority private key...")
    execute_process(
        COMMAND openssl req -new -newkey rsa:2048 -nodes -out ${CERTIFICATES_DIR}/ca.csr -keyout
                ${CERTIFICATES_DIR}/ca.key -nodes -subj "/CN=Aos Cloud" COMMAND_ERROR_IS_FATAL ANY
    )

    message("\nCreate a CA self-signed certificate...")
    execute_process(
        COMMAND openssl x509 -signkey ${CERTIFICATES_DIR}/ca.key -days 365 -req -in ${CERTIFICATES_DIR}/ca.csr -out
                ${CERTIFICATES_DIR}/ca.pem COMMAND_ERROR_IS_FATAL ANY
    )

    message("\nIssue a client certificate...")
    execute_process(COMMAND openssl genrsa -out ${CERTIFICATES_DIR}/client.key 2048 COMMAND_ERROR_IS_FATAL ANY)

    execute_process(
        COMMAND openssl req -new -key ${CERTIFICATES_DIR}/client.key -out ${CERTIFICATES_DIR}/client.csr -nodes -subj
                "/CN=Aos Core" COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
        COMMAND
            openssl x509 -req -days 365 -in ${CERTIFICATES_DIR}/client.csr -CA ${CERTIFICATES_DIR}/ca.pem -CAkey
            ${CERTIFICATES_DIR}/ca.key -set_serial 01 -out ${CERTIFICATES_DIR}/client.cer COMMAND_ERROR_IS_FATAL ANY
    )

    message("\nConvert PEM to DER...")
    execute_process(
        COMMAND openssl x509 -outform der -in ${CERTIFICATES_DIR}/ca.pem -out ${CERTIFICATES_DIR}/ca.cer.der
                COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
        COMMAND openssl x509 -outform der -in ${CERTIFICATES_DIR}/client.cer -out ${CERTIFICATES_DIR}/client.cer.der
                COMMAND_ERROR_IS_FATAL ANY
    )

    message("\nCreate a single certificate chain file...")
    file(WRITE ${CERTIFICATES_DIR}/client-ca-chain.pem "")
    file(READ ${CERTIFICATES_DIR}/client.cer CONTENT)
    file(APPEND ${CERTIFICATES_DIR}/client-ca-chain.pem "${CONTENT}")
    file(READ ${CERTIFICATES_DIR}/ca.pem CONTENT)
    file(APPEND ${CERTIFICATES_DIR}/client-ca-chain.pem "${CONTENT}")

    message("\nGenerating PKCS11 test certificates done!\n")

    target_compile_definitions(${TARGET} PUBLIC CERTIFICATES_DIR="${CERTIFICATES_DIR}")
endfunction()
