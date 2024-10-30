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

function(genchildcert CERTIFICATES_DIR FILE_NAME COMMONNAME PARENT_NAME)
    execute_process(
        COMMAND openssl req -new -keyout ${CERTIFICATES_DIR}/${FILE_NAME}.key -out ${CERTIFICATES_DIR}/${FILE_NAME}.csr
                -nodes -subj "/CN=${COMMONNAME}" COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
        COMMAND
            openssl x509 -req -days 365 -extfile ${CERTIFICATES_DIR}/extensions.conf -in
            ${CERTIFICATES_DIR}/${FILE_NAME}.csr -CA ${CERTIFICATES_DIR}/${PARENT_NAME}.cer -CAkey
            ${CERTIFICATES_DIR}/${PARENT_NAME}.key -out ${CERTIFICATES_DIR}/${FILE_NAME}.cer COMMAND_ERROR_IS_FATAL ANY
    )

endfunction()

function(gencertificates TARGET CERTIFICATES_DIR)
    file(MAKE_DIRECTORY ${CERTIFICATES_DIR})
    write_file("${CERTIFICATES_DIR}/extensions.conf" "basicConstraints = CA:TRUE\n")

    message("\nGenerating PKCS11 test certificates...")

    message("\nCreate a Certificate Authority private key...")
    execute_process(
        COMMAND openssl req -new -newkey rsa:2048 -nodes -out ${CERTIFICATES_DIR}/ca.csr -keyout
                ${CERTIFICATES_DIR}/ca.key -set_serial 42 -nodes -subj "/CN=Aos Cloud" COMMAND_ERROR_IS_FATAL ANY
    )

    message("\nCreate a CA self-signed certificate...")
    execute_process(
        COMMAND openssl x509 -extfile ${CERTIFICATES_DIR}/extensions.conf -signkey ${CERTIFICATES_DIR}/ca.key -days 365
                -req -in ${CERTIFICATES_DIR}/ca.csr -out ${CERTIFICATES_DIR}/ca.cer COMMAND_ERROR_IS_FATAL ANY
    )

    message("\nIssue client certificates...")
    genchildcert(${CERTIFICATES_DIR} "client_int" "client_int" "ca")
    genchildcert(${CERTIFICATES_DIR} "client" "client" "client_int")

    message("\nIssue server certificates...")
    genchildcert(${CERTIFICATES_DIR} "server_int" "server_int" "ca")
    genchildcert(${CERTIFICATES_DIR} "server" "localhost" "server_int")

    target_compile_definitions(${TARGET} PUBLIC CERTIFICATES_IAM_DIR="${CERTIFICATES_DIR}")
endfunction()
