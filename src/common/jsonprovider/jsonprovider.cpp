/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/JSON/Object.h>

#include <common/cloudprotocol/unitconfig.hpp>

#include "jsonprovider.hpp"

namespace aos::common::jsonprovider {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error JSONProvider::NodeConfigToJSON(const NodeConfig& nodeConfig, String& json) const
{
    auto jsonObj = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (auto err = common::cloudprotocol::ToJSON(nodeConfig, *jsonObj); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = json.Assign(utils::Stringify(*jsonObj).c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error JSONProvider::NodeConfigFromJSON(const String& json, NodeConfig& nodeConfig) const
{
    auto [jsonVar, err] = utils::ParseJson(json.CStr());
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = common::cloudprotocol::FromJSON(utils::CaseInsensitiveObjectWrapper(jsonVar), nodeConfig);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::jsonprovider
