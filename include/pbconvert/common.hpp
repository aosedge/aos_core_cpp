/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PBCONVERT_COMMON_HPP_
#define PBCONVERT_COMMON_HPP_

#include <google/protobuf/timestamp.pb.h>

#include <aos/common/tools/optional.hpp>
#include <aos/common/types.hpp>

#include <common/v1/common.grpc.pb.h>

namespace aos::common::pbconvert {

/**
 * Converts aos error to protobuf error.
 *
 * @param error aos error.
 * @return iamanager::v5::ErrorInfo.
 */
::common::v1::ErrorInfo ConvertAosErrorToProto(const Error& error);

/**
 * Converts aos instance ident to protobuf.
 *
 * @param src instance ident to convert.
 * @param[out] dst protobuf instance ident.
 * @return ::common::v1::InstanceIdent.
 */
::common::v1::InstanceIdent ConvertToProto(const InstanceIdent& src);

/**
 * Converts protobuf instance ident to aos.
 *
 * @param val protobuf instance ident.
 * @return InstanceIdent.
 */
InstanceIdent ConvertToAos(const ::common::v1::InstanceIdent& val);

/**
 * Converts protobuf timestamp to aos.
 *
 * @param val protobuf timestamp.
 * @return Optional<Time>.
 */
Optional<Time> ConvertToAos(const google::protobuf::Timestamp& val);

/**
 * Converts aos time to protobuf timestamp.
 *
 * @param time aos time.
 * @return google::protobuf::Timestamp .
 */
google::protobuf::Timestamp TimestampToPB(const aos::Time& time);

/**
 * Sets protobuf error message from aos.
 *
 * @param src aos error.
 * @param[out] dst protobuf message.
 * @return void.
 */
template <typename Message>
void SetErrorInfo(const Error& src, Message& dst)
{
    if (!src.IsNone()) {
        *dst.mutable_error() = ConvertAosErrorToProto(src);
    } else {
        dst.clear_error();
    }
}

} // namespace aos::common::pbconvert

#endif
