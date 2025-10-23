/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_PBCONVERT_IAM_HPP_
#define AOS_COMMON_PBCONVERT_IAM_HPP_

#include <core/common/crypto/itf/x509.hpp>
#include <core/common/types/common.hpp>
#include <core/common/types/permissions.hpp>

#include <iamanager/v6/iamanager.grpc.pb.h>

namespace aos::common::pbconvert {

/**
 * Converts aos subjects array to protobuf subjects.
 *
 * @param src aos subjects.
 * @return iamanager::v6::Subjects.
 */
iamanager::v6::Subjects ConvertToProto(const Array<StaticString<cIDLen>>& src);

/**
 * Converts aos node attribute to protobuf node attribute.
 *
 * @param src aos node attribute.
 * @return iamanager::v6::NodeAttribute.
 */
iamanager::v6::NodeAttribute ConvertToProto(const NodeAttribute& src);

/**
 * Converts aos partition info to protobuf partition info.
 *
 * @param src aos partition info.
 * @return iamanager::v6::PartitionInfo.
 */
iamanager::v6::PartitionInfo ConvertToProto(const PartitionInfo& src);

/**
 * Converts aos cpu info to protobuf cpu info.
 *
 * @param src aos cpu info.
 * @return iamanager::v6::CPUInfo.
 */
iamanager::v6::CPUInfo ConvertToProto(const CPUInfo& src);

/**
 * Converts aos node info to protobuf node info.
 *
 * @param src aos node info.
 * @return iamanager::v6::NodeInfo.
 */
iamanager::v6::NodeInfo ConvertToProto(const NodeInfo& src);

/**
 * Converts aos serial number to protobuf.
 *
 * @param src aos serial.
 * @return RetWithError<std::string>.
 */
RetWithError<std::string> ConvertSerialToProto(const StaticArray<uint8_t, crypto::cSerialNumSize>& src);

/**
 * Converts aos permissions request to protobuf permissions request.
 *
 * @param secret aos secret.
 * @param funcServerID aos functional server ID.
 * @return iamanager::v6::PermissionsRequest.
 */
iamanager::v6::PermissionsRequest ConvertToProto(const String& secret, const String& funcServerID);

/**
 * Converts protobuf permissions response to aos instance ident and function permissions.
 *
 * @param src protobuf permissions response.
 * @param[out] instanceIdent aos instance ident.
 * @param[out] servicePermissions aos function permissions.
 * @return Error.
 */
Error ConvertToAos(const iamanager::v6::PermissionsResponse& src, InstanceIdent& instanceIdent,
    Array<FunctionPermissions>& servicePermissions);

/**
 * Converts protobuf cert info to aos cert info.
 *
 * @param src protobuf cert info.
 * @param[out] dst aos cert info.
 * @return Error.
 */
Error ConvertToAos(const iamanager::v6::CertInfo& src, CertInfo& dst);

} // namespace aos::common::pbconvert

#endif
