/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_MOCKS_ROOTCERTSMOCK_HPP_
#define AOS_CM_COMMUNICATION_TESTS_MOCKS_ROOTCERTSMOCK_HPP_

#include <gmock/gmock.h>

#include <cm/rootcerts/itf/rootcerts.hpp>

namespace aos::cm::rootcerts {

/**
 * Root certificates mock.
 */
class RootCertsMock : public RootCertsItf {
public:
    MOCK_METHOD(Error, ProcessDesiredRootCertificates, (const DesiredUnitRootCertificates&), (override));
};

} // namespace aos::cm::rootcerts

#endif
