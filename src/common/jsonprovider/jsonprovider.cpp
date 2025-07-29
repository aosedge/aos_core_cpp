/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Parser.h>

#include <common/cloudprotocol/desiredstatus.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/time.hpp>

#include "jsonprovider.hpp"

namespace aos::common::jsonprovider {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error JSONProvider::NodeConfigToJSON(const sm::resourcemanager::NodeConfig& nodeConfig, String& json) const
{
    try {
        auto object = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        object->set("version", nodeConfig.mVersion.CStr());

        auto err = common::cloudprotocol::ToJSON(nodeConfig.mNodeConfig, *object);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to convert node config to JSON");

        json = utils::Stringify(object).c_str();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error JSONProvider::NodeConfigFromJSON(const String& json, sm::resourcemanager::NodeConfig& nodeConfig) const
{
    try {
        Poco::JSON::Parser                  parser;
        auto                                result = parser.parse(json.CStr());
        utils::CaseInsensitiveObjectWrapper object(result.extract<Poco::JSON::Object::Ptr>());

        auto err = nodeConfig.mVersion.Assign(object.GetValue<std::string>("version").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "parsed version length exceeds application limit");

        err = common::cloudprotocol::FromJSON(object, nodeConfig.mNodeConfig);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to parse node config from JSON");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::jsonprovider
