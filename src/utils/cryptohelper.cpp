/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <numeric>

#include "utils/cryptohelper.hpp"
#include "utils/exception.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

static std::string ConvertCertificateToPEM(
    const crypto::x509::Certificate& certificate, crypto::x509::ProviderItf& cryptoProvider)
{
    std::string result(crypto::cCertPEMLen, '0');
    String      view = result.c_str();

    auto err = cryptoProvider.X509CertToPEM(certificate, view);
    AOS_ERROR_CHECK_AND_THROW("Certificate conversion problem", err);

    result.resize(view.Size());

    return result;
}

static std::string ConvertCertificatesToPEM(
    const Array<crypto::x509::Certificate>& chain, crypto::x509::ProviderItf& cryptoProvider)
{
    return std::accumulate(
        chain.begin(), chain.end(), std::string {}, [&](const std::string& result, const auto& cert) {
            return result + ConvertCertificateToPEM(cert, cryptoProvider);
        });
}

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

RetWithError<std::string> LoadPEMCertificates(
    const String& certURL, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    try {
        auto [certificates, err] = certLoader.LoadCertsChainByURL(certURL);
        if (!err.IsNone()) {
            return {"", Error(err, "Load certificate by URL failed")};
        }

        auto chain = Array<crypto::x509::Certificate>(certificates->begin(), certificates->Size());

        return {ConvertCertificatesToPEM(chain, cryptoProvider), ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {"", utils::ToAosError(e)};
    }
}

} // namespace aos::common::utils
