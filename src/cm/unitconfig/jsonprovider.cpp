/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <common/jsonprovider/jsonprovider.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>

#include "jsonprovider.hpp"

namespace aos::cm::unitconfig {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error JSONProvider::UnitConfigFromJSON(const String& json, aos::UnitConfig& unitConfig) const
{
    try {
        Poco::JSON::Parser                          parser;
        auto                                        result = parser.parse(json.CStr());
        common::utils::CaseInsensitiveObjectWrapper object(result.extract<Poco::JSON::Object::Ptr>());

        auto err = unitConfig.mVersion.Assign(object.GetValue<std::string>("version").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed version length exceeds application limit");

        err = unitConfig.mFormatVersion.Assign(object.GetValue<std::string>("formatVersion").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed format version length exceeds application limit");

        if (object.Has("nodes")) {
            common::utils::ForEach(object, "nodes", [&unitConfig](const Poco::Dynamic::Var& value) {
                common::utils::CaseInsensitiveObjectWrapper nodeObject(value);

                NodeConfig nodeConfig = {};

                auto err = common::jsonprovider::NodeConfigFromJSONObject(nodeObject, nodeConfig);
                AOS_ERROR_CHECK_AND_THROW(err, "node config parsing error");

                err = unitConfig.mNodes.PushBack(nodeConfig);
                AOS_ERROR_CHECK_AND_THROW(err, "parsed nodes count exceeds application limit");
            });
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error JSONProvider::UnitConfigToJSON(const aos::UnitConfig& unitConfig, String& json) const
{
    try {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("version", unitConfig.mVersion.CStr());
        object->set("formatVersion", unitConfig.mFormatVersion.CStr());
        object->set(
            "nodes", common::utils::ToJsonArray(unitConfig.mNodes, common::jsonprovider::NodeConfigToJSONObject));

        json = common::utils::Stringify(object).c_str();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::unitconfig
