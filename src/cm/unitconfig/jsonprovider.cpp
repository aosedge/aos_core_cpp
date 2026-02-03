/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/cloudprotocol/unitconfig.hpp>

#include "jsonprovider.hpp"

namespace aos::cm::unitconfig {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error JSONProvider::UnitConfigFromJSON(const String& json, aos::UnitConfig& unitConfig) const
{
    auto [jsonVar, err] = common::utils::ParseJson(json.CStr());
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = common::cloudprotocol::FromJSON(common::utils::CaseInsensitiveObjectWrapper(jsonVar), unitConfig);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error JSONProvider::UnitConfigToJSON(const aos::UnitConfig& unitConfig, String& json) const
{
    auto jsonObj = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    if (auto err = common::cloudprotocol::ToJSON(unitConfig, *jsonObj); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = json.Assign(common::utils::Stringify(*jsonObj).c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::unitconfig
