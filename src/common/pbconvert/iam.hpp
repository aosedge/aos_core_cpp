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

#include <iamanager/v7/iamanager.grpc.pb.h>

namespace aos::common::pbconvert {

/**
 * Converts aos subjects array to protobuf subjects.
 *
 * @param src aos subjects.
 * @return iamanager::v7::Subjects.
 */
iamanager::v7::Subjects ConvertToProto(const Array<StaticString<cIDLen>>& src);

/**
 * Converts aos node attribute to protobuf node attribute.
 *
 * @param src aos node attribute.
 * @return iamanager::v7::NodeAttribute.
 */
iamanager::v7::NodeAttribute ConvertToProto(const NodeAttribute& src);

/**
 * Converts aos partition info to protobuf partition info.
 *
 * @param src aos partition info.
 * @return iamanager::v7::PartitionInfo.
 */
iamanager::v7::PartitionInfo ConvertToProto(const PartitionInfo& src);

/**
 * Converts aos cpu info to protobuf cpu info.
 *
 * @param src aos cpu info.
 * @return iamanager::v7::CPUInfo.
 */
iamanager::v7::CPUInfo ConvertToProto(const CPUInfo& src);

/**
 * Converts aos node info to protobuf node info.
 *
 * @param src aos node info.
 * @return iamanager::v7::NodeInfo.
 */
iamanager::v7::NodeInfo ConvertToProto(const NodeInfo& src);

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
 * @return iamanager::v7::PermissionsRequest.
 */
iamanager::v7::PermissionsRequest ConvertToProto(const String& secret, const String& funcServerID);

/**
 * Converts protobuf permissions response to aos instance ident and function permissions.
 *
 * @param src protobuf permissions response.
 * @param[out] instanceIdent aos instance ident.
 * @param[out] servicePermissions aos function permissions.
 * @return Error.
 */
Error ConvertToAos(const iamanager::v7::PermissionsResponse& src, InstanceIdent& instanceIdent,
    Array<FunctionPermissions>& servicePermissions);

/**
 * Converts protobuf cert info to aos cert info.
 *
 * @param src protobuf cert info.
 * @param[out] dst aos cert info.
 * @return Error.
 */
Error ConvertToAos(const iamanager::v7::CertInfo& src, CertInfo& dst);

} // namespace aos::common::pbconvert

#endif
