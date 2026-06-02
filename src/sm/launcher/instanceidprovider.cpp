/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/utils.hpp>

#include "instanceidprovider.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error InstanceIDProvider::GetInstanceID(const InstanceIdent& instance, String& instanceID) const
{
    auto idStr = std::string(instance.mItemID.CStr()) + ":" + std::string(instance.mSubjectID.CStr()) + ":"
        + std::to_string(instance.mInstance);

    if (auto err = instanceID.Assign(common::utils::NameUUID(idStr).c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
