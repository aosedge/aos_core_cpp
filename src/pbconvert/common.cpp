/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pbconvert/common.hpp"

namespace aos::common::pbconvert {

::common::v1::ErrorInfo ConvertAosErrorToProto(const Error& error)
{
    ::common::v1::ErrorInfo result;

    result.set_aos_code(static_cast<int32_t>(error.Value()));
    result.set_exit_code(error.Errno());

    if (!error.IsNone()) {
        StaticString<cErrorMessageLen> message;

        auto err = message.Convert(error);

        result.set_message(err.IsNone() ? message.CStr() : error.Message());
    }

    return result;
}

::common::v1::InstanceIdent ConvertToProto(const InstanceIdent& src)
{
    ::common::v1::InstanceIdent result;

    result.set_service_id(src.mServiceID.CStr());
    result.set_subject_id(src.mSubjectID.CStr());
    result.set_instance(src.mInstance);

    return result;
}

InstanceIdent ConvertToAos(const ::common::v1::InstanceIdent& val)
{
    InstanceIdent result;

    result.mServiceID = val.service_id().c_str();
    result.mSubjectID = val.subject_id().c_str();
    result.mInstance  = val.instance();

    return result;
}

Optional<Time> ConvertToAos(const google::protobuf::Timestamp& val)
{
    Optional<Time> result;

    if (val.seconds() > 0) {
        result.SetValue(Time::Unix(val.seconds(), val.nanos()));
    }

    return result;
}

google::protobuf::Timestamp TimestampToPB(const aos::Time& time)
{
    auto unixTime = time.UnixTime();

    google::protobuf::Timestamp result;

    result.set_seconds(unixTime.tv_sec);
    result.set_nanos(static_cast<int32_t>(unixTime.tv_nsec));

    return result;
}

} // namespace aos::common::pbconvert
