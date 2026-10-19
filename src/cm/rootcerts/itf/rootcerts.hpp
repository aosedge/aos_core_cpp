/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_ROOTCERTS_ITF_ROOTCERTS_HPP_
#define AOS_CM_ROOTCERTS_ITF_ROOTCERTS_HPP_

#include <core/common/types/certificates.hpp>

namespace aos::cm::rootcerts {

/**
 * Root certificates interface.
 */
class RootCertsItf {
public:
    /**
     * Destructor.
     */
    virtual ~RootCertsItf() = default;

    /**
     * Processes desired unit root certificates.
     *
     * @param desiredRootCerts desired unit root certificates.
     * @return Error.
     */
    virtual Error ProcessDesiredRootCertificates(const DesiredUnitRootCertificates& desiredRootCerts) = 0;
};

} // namespace aos::cm::rootcerts

#endif
