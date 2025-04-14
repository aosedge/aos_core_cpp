/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PK11_URI_H
#define PK11_URI_H

#include <openssl/asn1t.h>

/**
 * P11PROV_PK11_URI routines taken from pkcs11-provider:
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
