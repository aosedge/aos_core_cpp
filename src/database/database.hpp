/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DATABASE_HPP_
#define DATABASE_HPP_

#include <memory>
#include <optional>
#include <string>

#include <Poco/Data/Session.h>
#include <Poco/JSON/Object.h>

#include <aos/iam/certmodules/certmodule.hpp>
#include <aos/iam/nodemanager.hpp>
#include <config/config.hpp>
#include <migration/migration.hpp>

class Database : public aos::iam::certhandler::StorageItf, public aos::iam::nodemanager::NodeInfoStorageItf {
public:
    /**
     * Creates database instance.
     */
    Database();

    /**
     * Initializes certificate info storage.
     *
     * @param workDir working directory.
     * @param migrationConf migration configuration.
     * @return Error.
     */
    aos::Error Init(const std::string& workDir, const MigrationConfig& migrationConf);

    //
    // certhandler::StorageItf interface
    //

    /**
     * Adds new certificate info to the storage.
     *
     * @param certType certificate type.
     * @param certInfo certificate information.
     * @return Error.
     */
    aos::Error AddCertInfo(const aos::String& certType, const aos::iam::certhandler::CertInfo& certInfo) override;

    /**
     * Returns information about certificate with specified issuer and serial number.
     *
     * @param issuer certificate issuer.
     * @param serial serial number.
     * @param cert result certificate.
     * @return Error.
     */
    aos::Error GetCertInfo(const aos::Array<uint8_t>& issuer, const aos::Array<uint8_t>& serial,
        aos::iam::certhandler::CertInfo& cert) override;

    /**
     * Returns info for all certificates with specified certificate type.
     *
     * @param certType certificate type.
     * @param certsInfo result certificates info.
     * @return Error.
     */
    aos::Error GetCertsInfo(
        const aos::String& certType, aos::Array<aos::iam::certhandler::CertInfo>& certsInfo) override;

    /**
     * Removes certificate with specified certificate type and url.
     *
     * @param certType certificate type.
     * @param certURL certificate URL.
     * @return Error.
     */
    aos::Error RemoveCertInfo(const aos::String& certType, const aos::String& certURL) override;

    /**
     * Removes all certificates with specified certificate type.
     *
     * @param certType certificate type.
     * @return Error.
     */
    aos::Error RemoveAllCertsInfo(const aos::String& certType) override;

    //
    // nodemanager::NodeInfoStorageItf interface
    //

    /**
     * Updates whole information for a node.
     *
     * @param info node info.
     * @return Error.
     */
    aos::Error SetNodeInfo(const aos::NodeInfo& info) override;

    /**
     * Returns node info.
     *
     * @param nodeID node identifier.
     * @param[out] nodeInfo result node identifier.
     * @return Error.
     */
    aos::Error GetNodeInfo(const aos::String& nodeID, aos::NodeInfo& nodeInfo) const override;

    /**
     * Returns ids for all the node in the manager.
     *
     * @param ids result node identifiers.
     * @return Error.
     */
    aos::Error GetAllNodeIds(aos::Array<aos::StaticString<aos::cNodeIDLen>>& ids) const override;

    /**
     * Removes node info by its id.
     *
     * @param nodeID node identifier.
     * @return Error.
     */
    aos::Error RemoveNodeInfo(const aos::String& nodeID) override;

    /**
     * Destroys certificate info storage.
     */
    ~Database();

private:
    enum CertColumns { eType = 0, eIssuer, eSerial, eCertURL, eKeyURL, eNotAfter };
    using CertInfo = Poco::Tuple<std::string, Poco::Data::BLOB, Poco::Data::BLOB, std::string, std::string, uint64_t>;

    constexpr static int  cVersion    = 0;
    constexpr static auto cDBFileName = "iamanager.db";

    void     CreateTables();
    CertInfo ToAosCertInfo(const aos::String& certType, const aos::iam::certhandler::CertInfo& certInfo);
    void     FromAosCertInfo(const CertInfo& certInfo, aos::iam::certhandler::CertInfo& result);

    static Poco::JSON::Object ConvertNodeInfoToJSON(const aos::NodeInfo& nodeInfo);
    static aos::Error         ConvertNodeInfoFromJSON(const Poco::JSON::Object& src, aos::NodeInfo& dst);

    static Poco::JSON::Array ConvertCpuInfoToJSON(const aos::Array<aos::CPUInfo>& cpuInfo);
    static aos::Error        ConvertCpuInfoFromJSON(const Poco::JSON::Array& src, aos::Array<aos::CPUInfo>& dst);

    static Poco::JSON::Array ConvertPartitionInfoToJSON(const aos::Array<aos::PartitionInfo>& partitionInfo);
    static aos::Error ConvertPartitionInfoFromJSON(const Poco::JSON::Array& src, aos::Array<aos::PartitionInfo>& dst);

    static Poco::JSON::Array ConvertAttributesToJSON(const aos::Array<aos::NodeAttribute>& attributes);
    static aos::Error ConvertAttributesFromJSON(const Poco::JSON::Array& src, aos::Array<aos::NodeAttribute>& dst);

    std::unique_ptr<Poco::Data::Session>             mSession;
    std::optional<aos::common::migration::Migration> mMigration;
};

#endif
