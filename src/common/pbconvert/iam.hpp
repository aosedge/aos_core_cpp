/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PBCONVERT_IAM_HPP_
#define PBCONVERT_IAM_HPP_

#include <aos/common/crypto/crypto.hpp>
#include <aos/common/types.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

namespace aos::common::pbconvert {

/**
 * Converts aos subjects array to protobuf subjects.
 *
 * @param src aos subjects.
 * @return iamanager::v5::Subjects.
 */
iamanager::v5::Subjects ConvertToProto(const Array<StaticString<cSubjectIDLen>>& src);

/**
 * Converts aos node attribute to protobuf node attribute.
 *
 * @param src aos node attribute.
 * @return iamanager::v5::NodeAttribute.
 */
iamanager::v5::NodeAttribute ConvertToProto(const NodeAttribute& src);

/**
 * Converts aos partition info to protobuf partition info.
 *
 * @param src aos partition info.
 * @return iamanager::v5::PartitionInfo.
 */
iamanager::v5::PartitionInfo ConvertToProto(const PartitionInfo& src);

/**
 * Converts aos cpu info to protobuf cpu info.
 *
 * @param src aos cpu info.
 * @return iamanager::v5::CPUInfo.
 */
iamanager::v5::CPUInfo ConvertToProto(const CPUInfo& src);

/**
 * Converts aos node info to protobuf node info.
 *
 * @param src aos node info.
 * @return iamanager::v5::NodeInfo.
 */
iamanager::v5::NodeInfo ConvertToProto(const NodeInfo& src);

/**
 * Converts aos serial number to protobuf.
 *
 * @param src aos serial.
 * @return RetWithError<std::string>.
 */
RetWithError<std::string> ConvertSerialToProto(const StaticArray<uint8_t, crypto::cSerialNumSize>& src);

} // namespace aos::common::pbconvert

#endif
