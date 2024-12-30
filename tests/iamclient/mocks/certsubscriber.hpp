/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CERTSUBSCRIBER_HPP_
#define CERTSUBSCRIBER_HPP_

#include <gmock/gmock.h>

#include <aos/iam/certhandler.hpp>

namespace testing {

class MockCertSubscriber : public aos::iam::certhandler::CertReceiverItf {
public:
    MockCertSubscriber()          = default;
    virtual ~MockCertSubscriber() = default;
    MOCK_METHOD(void, OnCertChanged, (const aos::iam::certhandler::CertInfo&), (override));
};

} // namespace testing

#endif // CERTSUBSCRIBER_HPP_
