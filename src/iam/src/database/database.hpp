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

namespace aos::iam::database {

class Database : public iam::certhandler::StorageItf, public iam::nodemanager::NodeInfoStorageItf {
public:
    /**
     * Creates database instance.
     */
    Database();

    /**
     * Initializes certificate info storage.
     *
     * @param config database configuration.
     * @return Error.
     */
    Error Init(const config::DatabaseConfig& config);

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
    Error AddCertInfo(const String& certType, const iam::certhandler::CertInfo& certInfo) override;

    /**
     * Returns information about certificate with specified issuer and serial number.
     *
     * @param issuer certificate issuer.
     * @param serial serial number.
     * @param cert result certificate.
     * @return Error.
     */
    Error GetCertInfo(
        const Array<uint8_t>& issuer, const Array<uint8_t>& serial, iam::certhandler::CertInfo& cert) override;

    /**
     * Returns info for all certificates with specified certificate type.
     *
     * @param certType certificate type.
     * @param certsInfo result certificates info.
     * @return Error.
     */
    Error GetCertsInfo(const String& certType, Array<iam::certhandler::CertInfo>& certsInfo) override;

    /**
     * Removes certificate with specified certificate type and url.
     *
     * @param certType certificate type.
     * @param certURL certificate URL.
     * @return Error.
     */
    Error RemoveCertInfo(const String& certType, const String& certURL) override;

    /**
     * Removes all certificates with specified certificate type.
     *
     * @param certType certificate type.
     * @return Error.
     */
    Error RemoveAllCertsInfo(const String& certType) override;

    //
    // nodemanager::NodeInfoStorageItf interface
    //

    /**
     * Updates whole information for a node.
     *
     * @param info node info.
     * @return Error.
     */
    Error SetNodeInfo(const NodeInfo& info) override;

    /**
     * Returns node info.
     *
     * @param nodeID node identifier.
     * @param[out] nodeInfo result node identifier.
     * @return Error.
     */
    Error GetNodeInfo(const String& nodeID, NodeInfo& nodeInfo) const override;

    /**
     * Returns ids for all the node in the manager.
     *
     * @param ids result node identifiers.
     * @return Error.
     */
    Error GetAllNodeIds(Array<StaticString<cNodeIDLen>>& ids) const override;

    /**
     * Removes node info by its id.
     *
     * @param nodeID node identifier.
     * @return Error.
     */
    Error RemoveNodeInfo(const String& nodeID) override;

    /**
     * Destroys certificate info storage.
     */
    ~Database();

private:
    enum CertColumns { eType = 0, eIssuer, eSerial, eCertURL, eKeyURL, eNotAfter };
    using CertInfo = Poco::Tuple<std::string, Poco::Data::BLOB, Poco::Data::BLOB, std::string, std::string, uint64_t>;

    constexpr static int  cVersion    = 1;
    constexpr static auto cDBFileName = "iamanager.db";

    // to be used in unit tests
    virtual int GetVersion() const;

    void CreateMigrationData(const config::DatabaseConfig& config);
    void DropMigrationData();

    void     CreateTables();
    CertInfo ToAosCertInfo(const String& certType, const iam::certhandler::CertInfo& certInfo);
    void     FromAosCertInfo(const CertInfo& certInfo, iam::certhandler::CertInfo& result);

    static Poco::JSON::Object ConvertNodeInfoToJSON(const NodeInfo& nodeInfo);
    static Error              ConvertNodeInfoFromJSON(const Poco::JSON::Object& src, NodeInfo& dst);

    static Poco::JSON::Array ConvertCpuInfoToJSON(const Array<CPUInfo>& cpuInfo);
    static Error             ConvertCpuInfoFromJSON(const Poco::JSON::Array& src, Array<CPUInfo>& dst);

    static Poco::JSON::Array ConvertPartitionInfoToJSON(const Array<PartitionInfo>& partitionInfo);
    static Error             ConvertPartitionInfoFromJSON(const Poco::JSON::Array& src, Array<PartitionInfo>& dst);

    static Poco::JSON::Array ConvertAttributesToJSON(const Array<NodeAttribute>& attributes);
    static Error             ConvertAttributesFromJSON(const Poco::JSON::Array& src, Array<NodeAttribute>& dst);

    std::unique_ptr<Poco::Data::Session>        mSession;
    std::optional<common::migration::Migration> mDatabase;
};

} // namespace aos::iam::database

#endif
