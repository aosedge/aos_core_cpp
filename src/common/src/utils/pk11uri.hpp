/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 * Copyright (C) 2022 Simo Sorce <simo@redhat.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PK11_URI_H
#define PK11_URI_H

#include <openssl/asn1t.h>

/**
 * Routines of this header are taken from pkcs11-provider codebase:
 * https://github.com/latchset/pkcs11-provider/blob/main/src/pk11_uri.h
 */

struct P11PROV_PK11_URI {
    ASN1_VISIBLESTRING* desc;
    ASN1_UTF8STRING*    uri;
};

constexpr auto cP11ProvDescURIFile = "PKCS#11 Provider URI v1.0";

P11PROV_PK11_URI* P11PROV_PK11_URI_new(void);
void              P11PROV_PK11_URI_free(P11PROV_PK11_URI* a);
int               PEM_write_bio_P11PROV_PK11_URI(BIO* out, const P11PROV_PK11_URI* x);

#endif
