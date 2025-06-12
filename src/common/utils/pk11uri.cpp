/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <openssl/pem.h>

#include "pk11uri.hpp"

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

constexpr auto cP11ProvPEMLabel = "PKCS#11 PROVIDER URI";

const ASN1_TEMPLATE cP11ProvPK11URISeqTT[] = {
    {(0), (0), __builtin_offsetof(P11PROV_PK11_URI, desc), "desc", (ASN1_VISIBLESTRING_it)},
    {(0), (0), __builtin_offsetof(P11PROV_PK11_URI, uri), "uri", (ASN1_UTF8STRING_it)},
};

const ASN1_ITEM* P11PROV_PK11_URI_it(void)
{
    static const ASN1_ITEM local_it = {0x1, 16, cP11ProvPK11URISeqTT,
        sizeof(cP11ProvPK11URISeqTT) / sizeof(ASN1_TEMPLATE), nullptr, sizeof(P11PROV_PK11_URI), "P11PROV_PK11_URI"};
    return &local_it;
}

} // namespace

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

P11PROV_PK11_URI
*P11PROV_PK11_URI_new(void)
{
    return reinterpret_cast<P11PROV_PK11_URI*>(ASN1_item_new(P11PROV_PK11_URI_it()));
}

void P11PROV_PK11_URI_free(P11PROV_PK11_URI* a)
{
    ASN1_item_free(reinterpret_cast<ASN1_VALUE*>(a), P11PROV_PK11_URI_it());
}

int i2d_P11PROV_PK11_URI(const P11PROV_PK11_URI* a, unsigned char** out)
{
    return ASN1_item_i2d(reinterpret_cast<const ASN1_VALUE*>(a), out, P11PROV_PK11_URI_it());
}

int PEM_write_bio_P11PROV_PK11_URI(BIO* out, const P11PROV_PK11_URI* x)
{
    return PEM_ASN1_write_bio(reinterpret_cast<i2d_of_void*>(&i2d_P11PROV_PK11_URI), cP11ProvPEMLabel, out, x, nullptr,
        nullptr, 0, nullptr, nullptr);
}
